//---------------------------------------------------------------------------
/*
	TJS2 Script Engine
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TJS Debugging support
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <algorithm>
#include "tjsDebug.h"
#include "tjsHashSearch.h"
#include "tjsInterCodeGen.h"
#include "tjsGlobalStringMap.h"

#ifdef ENABLE_DEBUGGER
#include <map>
#include <list>
#include "Debugger.h"
#include "Application.h"
#include "NativeEventQueue.h"
#endif // ENABLE_DEBUGGER

namespace TJS
{


//---------------------------------------------------------------------------
// ObjectHashMap : hash map to track object construction/destruction
//--------------------------------------------------------------------------
tTJSBinaryStream * TJSObjectHashMapLog = NULL;  // log file object
//---------------------------------------------------------------------------
enum tTJSObjectHashMapLogItemId
{
	liiEnd,     // 00 end of the log
	liiVersion, // 01 identify log structure version
	liiAdd,     // 02 add a object
	liiRemove,  // 03 remove a object
	liiSetType, // 04 set object type information
	liiSetFlag, // 05 set object flag
};
//---------------------------------------------------------------------------
template <typename T>
void TJSStoreLog(const T &object)
{
	TJSObjectHashMapLog->Write(&object, sizeof(object));
}
//---------------------------------------------------------------------------
template <typename T>
T TJSRestoreLog()
{
	T object;
	TJSObjectHashMapLog->Read(&object, sizeof(object));
	return object;
}
//---------------------------------------------------------------------------
template <>
void TJSStoreLog<ttstr>(const ttstr & which)
{
	// store a string into log stream
	tjs_int length = which.GetLen();
	TJSObjectHashMapLog->Write(&length, sizeof(length));
		// Note that this function does not care what the endian is.
	TJSObjectHashMapLog->Write(which.c_str(), length * sizeof(tjs_char));
}
//---------------------------------------------------------------------------
template <>
ttstr TJSRestoreLog<ttstr>()
{
	// restore a string from log stream
	tjs_int length;
	TJSObjectHashMapLog->Read(&length, sizeof(length));
	ttstr ret((tTJSStringBufferLength)(length));
	tjs_char *buf = ret.Independ();
	TJSObjectHashMapLog->Read(buf, length * sizeof(tjs_char));
	buf[length] = 0;
	ret.FixLen();
	return ret;
}
//---------------------------------------------------------------------------
template <>
void TJSStoreLog<tTJSObjectHashMapLogItemId>(const tTJSObjectHashMapLogItemId & id)
{
	// log item id
	char cid = id;
	TJSObjectHashMapLog->Write(&cid, sizeof(char));
}
//---------------------------------------------------------------------------
template <>
tTJSObjectHashMapLogItemId TJSRestoreLog<tTJSObjectHashMapLogItemId>()
{
	// restore item id
	char cid;
	if(TJSObjectHashMapLog->Read(&cid, sizeof(char)) != sizeof(char))
		return liiEnd;
	return (tTJSObjectHashMapLogItemId)(cid);
}
//---------------------------------------------------------------------------
struct tTJSObjectHashMapRecord
{
	ttstr History; // call history where the object was created
	ttstr Where; // a label which indicates where the object was created
	ttstr Type; // object type ("class Array" etc)
	tjs_uint32 Flags;

	tTJSObjectHashMapRecord() : Flags(TJS_OHMF_EXIST) {;}

	void StoreLog()
	{
		// store the object into log stream
		TJSStoreLog(History);
		TJSStoreLog(Where);
		TJSStoreLog(Type);
		TJSStoreLog(Flags);
	}

	void RestoreLog()
	{
		// restore the object from log stream
		History = TJSRestoreLog<ttstr>();
		Where   = TJSRestoreLog<ttstr>();
		Type    = TJSRestoreLog<ttstr>();
		Flags   = TJSRestoreLog<tjs_uint32>();
	}
};
//---------------------------------------------------------------------------
class tTJSObjectHashMapRecordComparator_HistoryAndType
{
public:
	bool operator () (const tTJSObjectHashMapRecord & lhs,
		const tTJSObjectHashMapRecord & rhs) const
	{
		if(lhs.Where == rhs.Where)
			return lhs.Type < rhs.Type;
		return lhs.History < rhs.History;
	}
};

class tTJSObjectHashMapRecordComparator_Type
{
public:
	bool operator () (const tTJSObjectHashMapRecord & lhs,
		const tTJSObjectHashMapRecord & rhs) const
	{
		return lhs.Type < rhs.Type;
	}
};
//---------------------------------------------------------------------------
class tTJSObjectHashMap;
tTJSObjectHashMap * TJSObjectHashMap;
class tTJSObjectHashMap
{
	typedef
		tTJSHashTable<void *, tTJSObjectHashMapRecord,
			tTJSHashFunc<void *>, 1024>
		tHash;
	tHash Hash;

	tjs_int RefCount;


public:
	tTJSObjectHashMap()
	{
		RefCount = 1;
		TJSObjectHashMap = this;
	}

protected:
	~tTJSObjectHashMap()
	{
		TJSObjectHashMap = NULL;
	}

protected:
	void _AddRef() { RefCount ++; }
	void _Release() { if(RefCount == 1) delete this; else RefCount --; }


public:
	static void AddRef()
	{
		if(TJSObjectHashMap)
			TJSObjectHashMap->_AddRef();
		else
			new tTJSObjectHashMap();
	}

	static void Release()
	{
		if(TJSObjectHashMap)
			TJSObjectHashMap->_Release();
	}


	void Add(void * object,
		const tTJSObjectHashMapRecord & record)
	{
		Hash.Add(object, record);
	}

	void SetType(void * object, const ttstr & info)
	{
		tTJSObjectHashMapRecord * rec;
		rec = Hash.Find(object);
		if(!rec) return;
		rec->Type = TJSMapGlobalStringMap(info);
	}

	ttstr GetType(void * object)
	{
		tTJSObjectHashMapRecord * rec;
		rec = Hash.Find(object);
		if(!rec) return ttstr();
		return rec->Type;
	}

	void SetFlag(void * object, tjs_uint32 flags_to_change, tjs_uint32 bits)
	{
		tTJSObjectHashMapRecord * rec;
		rec = Hash.Find(object);
		if(!rec) return;
		rec->Flags &=  (~flags_to_change);
		rec->Flags |=  (bits & flags_to_change);
	}

	tjs_uint32 GetFlag(void * object)
	{
		tTJSObjectHashMapRecord * rec;
		rec = Hash.Find(object);
		if(!rec) return 0;
		return rec->Flags;
	}

	void Remove(void * object)
	{
		Hash.Delete(object);
	}

	void WarnIfObjectIsDeleting(iTJSConsoleOutput * output, void * object)
	{
		tTJSObjectHashMapRecord * rec;
		rec = Hash.Find(object);
		if(!rec) return;

		if(rec->Flags & TJS_OHMF_DELETING)
		{
			// warn running code on deleting-in-progress object
			ttstr warn(TJSWarning);
			tjs_char tmp[64];
			TJS_snprintf(tmp, sizeof(tmp)/sizeof(tjs_char), TJS_W("0x%p"), object);

			ttstr info(TJSWarnRunningCodeOnDeletingObject);
			info.Replace(TJS_W("%1"), tmp);
			info.Replace(TJS_W("%2"), rec->Type);
			info.Replace(TJS_W("%3"), rec->Where);
			info.Replace(TJS_W("%4"), TJSGetStackTraceString(1));

			output->Print((warn + info).c_str());
		}
	}

	void ReportAllUnfreedObjects(iTJSConsoleOutput * output)
	{
		{
			ttstr msg = (const tjs_char *)TJSNObjectsWasNotFreed;
			msg.Replace(TJS_W("%1"), ttstr((tjs_int)Hash.GetCount()));
			output->Print(msg.c_str());
		}

		// list all unfreed objects
		tHash::tIterator i;
		for(i = Hash.GetFirst(); !i.IsNull(); i++)
		{
			tjs_char addr[65];
			TJS_snprintf(addr, sizeof(addr)/sizeof(tjs_char), TJS_W("0x%p"), i.GetKey());
			ttstr info = (const tjs_char *)TJSObjectWasNotFreed;
			info.Replace(TJS_W("%1"), addr);
			info.Replace(TJS_W("%2"), i.GetValue().Type);
			info.Replace(TJS_W("%3"), i.GetValue().History);
			output->Print(info.c_str());
		}

		// group by the history and object type
		output->Print(TJS_W("---"));
		output->Print((const tjs_char *)TJSGroupByObjectTypeAndHistory);
		std::vector<tTJSObjectHashMapRecord> items;
		for(i = Hash.GetFirst(); !i.IsNull(); i++)
			items.push_back(i.GetValue());

		std::stable_sort(items.begin(), items.end(),
			tTJSObjectHashMapRecordComparator_HistoryAndType());

		ttstr history, type;
		tjs_int count = 0;
		if(items.size() > 0)
		{
			for(std::vector<tTJSObjectHashMapRecord>::iterator i = items.begin();
				; i++)
			{
				if(i != items.begin() &&
					(i == items.end() || history != i->History || type != i->Type))
				{
					tjs_char tmp[64];
					TJS_snprintf(tmp, sizeof(tmp)/sizeof(tjs_char), TJS_W("%6d"), (int)count);
					ttstr info = (const tjs_char *)TJSObjectCountingMessageGroupByObjectTypeAndHistory;
					info.Replace(TJS_W("%1"), tmp);
					info.Replace(TJS_W("%2"), type);
					info.Replace(TJS_W("%3"), history);
					output->Print(info.c_str());

					if(i == items.end()) break;

					count = 0;
				}

				history = i->History;
				type = i->Type;
				count ++;
			}
		}

		// group by object type
		output->Print(TJS_W("---"));
		output->Print((const tjs_char *)TJSGroupByObjectType);
		std::stable_sort(items.begin(), items.end(),
			tTJSObjectHashMapRecordComparator_Type());

		type.Clear();
		count = 0;
		if(items.size() > 0)
		{
			for(std::vector<tTJSObjectHashMapRecord>::iterator i = items.begin();
				; i++)
			{
				if(i != items.begin() &&
					(i == items.end() || type != i->Type))
				{
					tjs_char tmp[64];
					TJS_snprintf(tmp, sizeof(tmp)/sizeof(tjs_char), TJS_W("%6d"), (int)count);
					ttstr info = (const tjs_char *)TJSObjectCountingMessageTJSGroupByObjectType;
					info.Replace(TJS_W("%1"), tmp);
					info.Replace(TJS_W("%2"), type);
					output->Print(info.c_str());

					if(i == items.end()) break;

					count = 0;
				}

				type = i->Type;
				count ++;
			}
		}
	}

	bool AnyUnfreed()
	{
		return Hash.GetCount() != 0;
	}

	void WriteAllUnfreedObjectsToLog()
	{
		if(!TJSObjectHashMapLog) return;
			
		tHash::tIterator i;
		for(i = Hash.GetFirst(); !i.IsNull(); i++)
		{
			TJSStoreLog(liiAdd);
			TJSStoreLog(i.GetKey());
			i.GetValue().StoreLog();
		}
	}

	void ReplayLog()
	{
		// replay recorded log
		while(true)
		{
			tTJSObjectHashMapLogItemId id = TJSRestoreLog<tTJSObjectHashMapLogItemId>();
			if(id == liiEnd)          // 00 end of the log
			{
				break;
			}
			else if(id == liiVersion) // 01 identify log structure version
			{
				tjs_int v = TJSRestoreLog<tjs_int>();
				if(v != TJSVersionHex)
					TJS_eTJSError( TJSObjectHashMapLogVersionMismatch );
			}
			else if(id == liiAdd)     // 02 add a object
			{
				void * object = TJSRestoreLog<void*>();
				tTJSObjectHashMapRecord rec;
				rec.RestoreLog();
				Add(object, rec);
			}
			else if(id == liiRemove)  // 03 remove a object
			{
				void * object = TJSRestoreLog<void*>();
				Remove(object);
			}
			else if(id == liiSetType) // 04 set object type information
			{
				void * object = TJSRestoreLog<void*>();
				ttstr type = TJSRestoreLog<ttstr>();
				SetType(object, type);
			}
			else if(id == liiSetFlag) // 05 set object flag
			{
				void * object = TJSRestoreLog<void*>();
				tjs_uint32 flags_to_change = TJSRestoreLog<tjs_uint32>();
				tjs_uint32 bits = TJSRestoreLog<tjs_uint32>();
				SetFlag(object, flags_to_change, bits);
			}
			else
			{
				TJS_eTJSError( TJSCurruptedObjectHashMapLog );
			}
		}
	}

};
//---------------------------------------------------------------------------
void TJSAddRefObjectHashMap()
{
	tTJSObjectHashMap::AddRef();
}
//---------------------------------------------------------------------------
void TJSReleaseObjectHashMap()
{
	tTJSObjectHashMap::Release();
}
//---------------------------------------------------------------------------
void TJSAddObjectHashRecord(void * object)
{
	if(!TJSObjectHashMap && !TJSObjectHashMapLog) return;

	// create object record and log
	tTJSObjectHashMapRecord rec;
	ttstr hist(TJSGetStackTraceString(4, (const tjs_char *)(TJSObjectCreationHistoryDelimiter)));
	if(hist.IsEmpty())
		hist = TJSMapGlobalStringMap((const tjs_char *)TJSCallHistoryIsFromOutOfTJS2Script);
	rec.History = hist;
	ttstr where(TJSGetStackTraceString(1));
	if(where.IsEmpty())
		where = TJSMapGlobalStringMap((const tjs_char *)TJSCallHistoryIsFromOutOfTJS2Script);
	rec.Where = where;
	static ttstr InitialType(TJS_W("unknown type"));
	rec.Type = InitialType;

	if(TJSObjectHashMap)
	{
		TJSObjectHashMap->Add(object, rec);
	}
	else if(TJSObjectHashMapLog)
	{
		TJSStoreLog(liiAdd);
		TJSStoreLog(object);
		rec.StoreLog();
	}
}
//---------------------------------------------------------------------------
void TJSRemoveObjectHashRecord(void * object)
{
	if(TJSObjectHashMap)
	{
		TJSObjectHashMap->Remove(object);
	}
	else if(TJSObjectHashMapLog)
	{
		TJSStoreLog(liiRemove);
		TJSStoreLog(object);
	}
}
//---------------------------------------------------------------------------
void TJSObjectHashSetType(void * object, const ttstr &type)
{
	if(TJSObjectHashMap)
	{
		TJSObjectHashMap->SetType(object, type);
	}
	else if(TJSObjectHashMapLog)
	{
		TJSStoreLog(liiSetType);
		TJSStoreLog(object);
		TJSStoreLog(type);
	}
}
//---------------------------------------------------------------------------
void TJSSetObjectHashFlag(void *object, tjs_uint32 flags_to_change, tjs_uint32 bits)
{
	if(TJSObjectHashMap)
	{
		TJSObjectHashMap->SetFlag(object, flags_to_change, bits);
	}
	else if(TJSObjectHashMapLog)
	{
		TJSStoreLog(liiSetFlag);
		TJSStoreLog(object);
		TJSStoreLog(flags_to_change);
		TJSStoreLog(bits);
	}
}
//---------------------------------------------------------------------------
void TJSReportAllUnfreedObjects(iTJSConsoleOutput * output)
{
	if(TJSObjectHashMap) TJSObjectHashMap->ReportAllUnfreedObjects(output);
}
//---------------------------------------------------------------------------
bool TJSObjectHashAnyUnfreed()
{
	if(TJSObjectHashMap) return TJSObjectHashMap->AnyUnfreed();
	return false;
}
//---------------------------------------------------------------------------
void TJSObjectHashMapSetLog(tTJSBinaryStream * stream)
{
	// Set log object. The log file object should not freed until
	// the program (the program is the Process, not RTL nor TJS2 framework).
	TJSObjectHashMapLog = stream;
	TJSStoreLog(liiVersion);
	TJSStoreLog(TJSVersionHex);
}
//---------------------------------------------------------------------------
void TJSWriteAllUnfreedObjectsToLog()
{
	if(TJSObjectHashMap && TJSObjectHashMapLog) TJSObjectHashMap->WriteAllUnfreedObjectsToLog();
}
//---------------------------------------------------------------------------
void TJSWarnIfObjectIsDeleting(iTJSConsoleOutput * output, void * object)
{
	if(TJSObjectHashMap) TJSObjectHashMap->WarnIfObjectIsDeleting(output, object);
}
//---------------------------------------------------------------------------
void TJSReplayObjectHashMapLog()
{
	if(TJSObjectHashMap && TJSObjectHashMapLog) TJSObjectHashMap->ReplayLog();
}
//---------------------------------------------------------------------------
ttstr TJSGetObjectTypeInfo(void * object)
{
	if(TJSObjectHashMap) return TJSObjectHashMap->GetType(object);
	return ttstr();
}
//---------------------------------------------------------------------------
tjs_uint32 TJSGetObjectHashCheckFlag(void * object)
{
	if(TJSObjectHashMap) return TJSObjectHashMap->GetFlag(object);
	return 0;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// StackTracer : stack to trace function call trace
//---------------------------------------------------------------------------
class tTJSStackTracer;
tTJSStackTracer * TJSStackTracer;
//---------------------------------------------------------------------------
struct tTJSStackRecord
{
	tTJSInterCodeContext * Context;
	const tjs_int * CodeBase;
	tjs_int * const * CodePtr;
	bool InTry;

	tTJSStackRecord(tTJSInterCodeContext * context, bool in_try)
	{
		CodeBase = NULL;
		CodePtr = NULL;
		InTry = in_try;
		Context = context;
		if(Context) Context->AddRef();
	}

	~tTJSStackRecord()
	{
		if(Context) Context->Release();
	}

	tTJSStackRecord(const tTJSStackRecord & rhs)
	{
		Context = NULL;
		*this = rhs;
	}

	void operator = (const tTJSStackRecord & rhs)
	{
		if(Context != rhs.Context)
		{
			if(Context) Context->Release(), Context = NULL;
			Context = rhs.Context;
			if(Context) Context->AddRef();
		}
		CodeBase = rhs.CodeBase;
		CodePtr = rhs.CodePtr;
		InTry = rhs.InTry;
	}
};

//---------------------------------------------------------------------------
class tTJSStackTracer
{
	std::vector<tTJSStackRecord> Stack;

	tjs_int RefCount;

public:
	tTJSStackTracer()
	{
		RefCount = 1;
		TJSStackTracer = this;
	}

protected:
	~tTJSStackTracer()
	{
		TJSStackTracer = NULL;
	}

protected:
	void _AddRef() { RefCount ++; }
	void _Release() { if(RefCount == 1) delete this; else RefCount --; }

public:
	static void AddRef()
	{
		if(TJSStackTracer)
			TJSStackTracer->_AddRef();
		else
			new tTJSStackTracer();
	}

	static void Release()
	{
		if(TJSStackTracer)
			TJSStackTracer->_Release();
	}

	void Push(tTJSInterCodeContext *context, bool in_try)
	{
		Stack.push_back(tTJSStackRecord(context, in_try));
	}

	void Pop()
	{
		Stack.pop_back();
	}

	void SetCodePointer(const tjs_int32 * codebase, tjs_int32 * const * codeptr)
	{
		tjs_uint size = (tjs_uint)Stack.size();
		if(size < 1) return;
		tjs_uint top = size - 1;
		Stack[top].CodeBase = codebase;
		Stack[top].CodePtr = codeptr;
	}

	ttstr GetTraceString(tjs_int limit, const tjs_char * delimiter)
	{
		// get stack trace string
		if(delimiter == NULL) delimiter = TJS_W(" <-- ");

		ttstr ret;
		tjs_int top = (tjs_int)(Stack.size() - 1);
		while(top >= 0)
		{
			if(!ret.IsEmpty()) ret += delimiter;

			const tTJSStackRecord & rec = Stack[top];
			ttstr str;
			if(rec.CodeBase && rec.CodePtr)
			{
				str = rec.Context->GetPositionDescriptionString(
					(tjs_int)(*rec.CodePtr - rec.CodeBase));
			}
			else
			{
				str = rec.Context->GetPositionDescriptionString(0);
			}

			ret += str;

			// skip try block stack.
			// 'try { } catch' blocks are implemented as sub-functions
			// in a parent function.
			while(top >= 0 && Stack[top].InTry) top--;

			// check limit
			if(limit)
			{
				limit --;
				if(limit <= 0) break;
			}

			top --;
		}

		return ret;
	}
};
//---------------------------------------------------------------------------
void TJSAddRefStackTracer()
{
	tTJSStackTracer::AddRef();
}
//---------------------------------------------------------------------------
void TJSReleaseStackTracer()
{
	tTJSStackTracer::Release();
}
//---------------------------------------------------------------------------
void TJSStackTracerPush(tTJSInterCodeContext *context, bool in_try)
{
	if(TJSStackTracer)
		TJSStackTracer->Push(context, in_try);
}
//---------------------------------------------------------------------------
void TJSStackTracerSetCodePointer(const tjs_int32 * codebase,
	tjs_int32 * const * codeptr)
{
	if(TJSStackTracer)
		TJSStackTracer->SetCodePointer(codebase, codeptr);
}
//---------------------------------------------------------------------------
void TJSStackTracerPop()
{
	if(TJSStackTracer)
		TJSStackTracer->Pop();
}
//---------------------------------------------------------------------------
ttstr TJSGetStackTraceString(tjs_int limit, const tjs_char *delimiter)
{
	if(TJSStackTracer)
		return TJSStackTracer->GetTraceString(limit, delimiter);
	else
		return ttstr();
}
//---------------------------------------------------------------------------

#if ENABLE_DEBUGGER
// 名前コレクション
// ファイル名とかクラス名とか関数名とか変数名を入れて、インデックスに変換して管理する
class NameIndexCollection
{
protected:
	std::map<std::wstring,int> NameWithID;	//!< 名前とIDのペア
	std::vector<const std::wstring*> Names;	//!< 指定インデックスの名前

public:
	int GetID( const std::wstring& name ) {
		std::map<std::wstring,int>::const_iterator i = NameWithID.find( name );
		if( i != NameWithID.end() ) {
			return i->second;
		}
		// 見付からないので追加する
		int index = Names.size();	// 配列の最後の要素番号を得る
		typedef std::pair<std::map<std::wstring,int>::iterator, bool> name_result_t;
		name_result_t ret = NameWithID.insert( std::make_pair( name, index ) );	// 要素番号で挿入
		assert( ret.second );
		const std::wstring* name_ref = &((*(ret.first)).first);	// 挿入した名前のポイントを得る
		Names.push_back( name_ref );	// そのポインタを配列に保存する
		return index;
	}
	const std::wstring* GetName( int id ) const {
		if( id < static_cast<int>(Names.size()) ) {
			return Names[id];
		}
		return NULL;
	}
};
static NameIndexCollection NameIndexCollectionData;
int TJSGetIDFromName( const tjs_char* name ) { return NameIndexCollectionData.GetID( std::wstring(name) ); }
const std::wstring* TJSGetNameFormID( int id ) { return NameIndexCollectionData.GetName( id ); }

void TJSDebuggerGetScopeKey( ScopeKey& scope, const tjs_char* classname, const tjs_char* funcname, const tjs_char* filename, int codeoffset ) {
	int classindex = -1;
	if( classname && classname[0] ) classindex = TJSGetIDFromName(classname);

	int funcindex = -1;
	if( funcname && funcname[0] ) funcindex = TJSGetIDFromName(funcname);

	int fileindex = -1;
	if( filename && filename[0] ) fileindex = TJSGetIDFromName(filename);

	scope.Set(classindex,funcindex,fileindex,codeoffset);
}


struct LocalVariableKey {
	int VarIndex;	//!< 変数名インデックス
	int RegAddr;	//!< レジスタアドレス

	LocalVariableKey( int var, int reg )
	: VarIndex(var), RegAddr(reg)
	{}
	LocalVariableKey()
	: VarIndex(-1), RegAddr(-1)
	{}
	LocalVariableKey( const LocalVariableKey& rhs ) {
		VarIndex = rhs.VarIndex;
		RegAddr = rhs.RegAddr;
	}
	void Set( int var, int reg ) {
		VarIndex = var;
		RegAddr = reg;
	}
};

class ClassVariableCollection {
	typedef LocalVariableKey ClassVariableKey;

	typedef std::map<int,std::list<ClassVariableKey> > variable_map_t;
	typedef variable_map_t::iterator		iterator;
	typedef variable_map_t::const_iterator	const_iterator;
	variable_map_t	Variables;

public:
	void ClearVar( const tjs_char* classname ) {
		int classindex = -1;
		if( classname && classname[0] ) classindex = TJSGetIDFromName(classname);

		variable_map_t::iterator i = Variables.find( classindex );
		if( i != Variables.end() ) {
			i->second.clear();
		}
	}

	void SetVar( const tjs_char* classname, const tjs_char* varname, int regaddr ) {
		int classindex = -1;
		if( classname && classname[0] ) classindex = TJSGetIDFromName(classname);

		int varindex = -1;
		if( varname && varname[0] ) varindex = TJSGetIDFromName(varname);

		variable_map_t::iterator i = Variables.find( classindex );
		if( i != Variables.end() ) {
			i->second.push_back( ClassVariableKey( varindex, regaddr ) );
			return;
		}
		// 見付からないので追加する
		typedef std::pair<iterator, bool> result_t;
		result_t ret = Variables.insert( std::make_pair( classindex, std::list<ClassVariableKey>() ) );
		assert( ret.second );
		ret.first->second.push_back( LocalVariableKey( varindex, regaddr ) );
	}
	// 変数名と値のリストを得る
	void GetVars( const tjs_char* classname, tTJSVariant* ra, tTJSVariant* da, std::list<std::wstring>& values ) {
		values.clear();
		if( ra == NULL || da == NULL ) return;

		int classindex = -1;
		if( classname && classname[0] ) classindex = TJSGetIDFromName(classname);

		iterator i = Variables.find( classindex );
		if( i != Variables.end() ) {
			std::wstring selfclass(L"this.");
			if( classindex < 0 ) selfclass = std::wstring(L"global.");
			const std::list<ClassVariableKey>& vars = i->second;
			for( std::list<ClassVariableKey>::const_iterator j = vars.begin(); j != vars.end(); ++j ) {
				const std::wstring* varname = TJSGetNameFormID( (*j).VarIndex );
				std::wstring memname;
				if( varname ) {
					memname = selfclass + *varname;
				} else {
					memname = selfclass + std::wstring(L"(unknown)");
				}
				tTJSVariant* ra_code1 = TJS_GET_VM_REG_ADDR( ra, TJS_TO_VM_REG_ADDR(-1) );
				tjs_error hr = -1;
				tTJSVariant result;
				if( varname && ra_code1->Type() == tvtObject && ra_code1->AsObjectNoAddRef() ) {
					tTJSVariantClosure clo = ra_code1->AsObjectClosureNoAddRef();
					if( clo.Object ) {
						hr = clo.PropGet( 0, varname->c_str(), NULL, &result, clo.ObjThis?clo.ObjThis:ra[-1].AsObjectNoAddRef());
					} else {
						hr = -1;
					}
				}

				std::wstring value;
				if( TJS_FAILED(hr) ) {
					value = std::wstring(L"error");
				} else {
					value = std::wstring( TJSVariantToReadableString( result ).c_str() );
				}

				values.push_back( memname );
				values.push_back( value );
			}
		}
	}
};

class LocalVariableCollection {
	typedef std::map<ScopeKey,std::list<LocalVariableKey> > variable_map_t;
	typedef variable_map_t::iterator		iterator;
	typedef variable_map_t::const_iterator	const_iterator;
	variable_map_t	Variables;
public:
	void SetVar( const ScopeKey& scope, const tjs_char* varname, int regaddr ) {
		int varindex = -1;
		if( varname && varname[0] ) varindex = TJSGetIDFromName(varname);

		iterator i = Variables.find( scope );
		if( i != Variables.end() ) {
			// 既にキーが存在するので、そこに追加する
			i->second.push_back( LocalVariableKey(varindex,regaddr) );
			return;
		}
		// 見付からないので追加する
		typedef std::pair<iterator, bool> result_t;
		result_t ret = Variables.insert( std::make_pair( scope, std::list<LocalVariableKey>() ) );
		assert( ret.second );
		ret.first->second.push_back( LocalVariableKey( varindex, regaddr ) );
	}

	void SetVar( const tjs_char* classname, const tjs_char* funcname, const tjs_char* filename, int codeoffset, const tjs_char* varname, int regaddr ) {
		ScopeKey scope;
		TJSDebuggerGetScopeKey( scope, classname, funcname, filename, codeoffset );
		SetVar( scope, varname, regaddr );
	}
	void ClearVar( const ScopeKey& scope ) {
		iterator i = Variables.find( scope );
		if( i != Variables.end() ) {
			i->second.clear();
		}
	}
	void ClearVar( const tjs_char* classname, const tjs_char* funcname, const tjs_char* filename, int codeoffset ) {
		ScopeKey scope;
		TJSDebuggerGetScopeKey( scope, classname, funcname, filename, codeoffset );
		ClearVar( scope );
	}

	// 変数名と値のリストを得る
	void GetVars( const ScopeKey& scope, tTJSVariant* ra, std::list<std::wstring>& values ) {
		values.clear();
		if( ra == NULL ) return;
		iterator i = Variables.find( scope );
		if( i != Variables.end() ) {
			const std::list<LocalVariableKey>& vars = i->second;
			for( std::list<LocalVariableKey>::const_iterator j = vars.begin(); j != vars.end(); ++j ) {
				const std::wstring* varname = TJSGetNameFormID( (*j).VarIndex );
				std::wstring name;
				if( varname ) {
					name = *varname;
				} else {
					name = std::wstring(L"(unknown)");
				}
				std::wstring value( TJSVariantToReadableString( TJS_GET_VM_REG( ra, (*j).RegAddr ) ).c_str() );

				values.push_back( name );
				values.push_back( value );
			}
		}
	}
	// 変数名と値のリストを得る
	void GetVars( const tjs_char* classname, const tjs_char* funcname, const tjs_char* filename, int codeoffset, tTJSVariant* ra, std::list<std::wstring>& values ) {
		ScopeKey scope;
		TJSDebuggerGetScopeKey( scope, classname, funcname, filename, codeoffset );
		GetVars( scope, ra, values );
	}
};
static LocalVariableCollection LocalVariableCollectionData;
static ClassVariableCollection ClassVariableCollectionData;

// codeoffset 関数コール前のコードのオフセット
void TJSDebuggerAddLocalVariable( const tjs_char* classname, const tjs_char* funcname, const tjs_char* filename, int codeoffset, const tjs_char* varname, int regaddr ) {
	LocalVariableCollectionData.SetVar( classname, funcname, filename, codeoffset, varname, regaddr );
}
void TJSDebuggerGetLocalVariableString( const tjs_char* classname, const tjs_char* funcname, const tjs_char* filename, int codeoffset, tTJSVariant* ra, std::list<std::wstring>& values ) {
	LocalVariableCollectionData.GetVars( classname, funcname, filename, codeoffset, ra, values );
}

void TJSDebuggerAddLocalVariable( const ScopeKey& key, const tjs_char* varname, int regaddr ) {
	LocalVariableCollectionData.SetVar( key, varname, regaddr );
}
void TJSDebuggerGetLocalVariableString( const ScopeKey& key, tTJSVariant* ra, std::list<std::wstring>& values ) {
	LocalVariableCollectionData.GetVars( key, ra, values );
}
void TJSDebuggerClearLocalVariable( const ScopeKey& key ) {
	LocalVariableCollectionData.ClearVar( key );
}
void TJSDebuggerClearLocalVariable( const tjs_char* classname, const tjs_char* funcname, const tjs_char* filename, int codeoffset ) {
	LocalVariableCollectionData.ClearVar( classname, funcname, filename, codeoffset );
}

// クラスメンバ変数の初期化
void TJSDebuggerAddClassVariable( const tjs_char* classname, const tjs_char* varname, int regaddr ) {
	ClassVariableCollectionData.SetVar( classname, varname, regaddr );
}
void TJSDebuggerGetClassVariableString( const tjs_char* classname, tTJSVariant* ra, tTJSVariant* da, std::list<std::wstring>& values ) {
	ClassVariableCollectionData.GetVars( classname, ra, da, values );
}
void TJSDebuggerClearLocalVariable( const tjs_char* classname ) {
	ClassVariableCollectionData.ClearVar( classname );
}
void TJSDebuggerLog(const ttstr &line, bool impotant) {}
void TJSDebuggerHook(tjs_int evtype, const tjs_char *filename, tjs_int lineno, tTJSInterCodeContext* ctx) {}
#endif	// ENABLE_DEBUGGER

//---------------------------------------------------------------------------


} // namespace TJS

