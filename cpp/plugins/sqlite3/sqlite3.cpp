#include <stdio.h>
#include <string>
#include "ncbind/ncbind.hpp"
#include "sqlite3.h"
#include "xp3_vfs.h"
#include "SDL3/SDL_stdinc.h"

#include "TVPStorage.h"
#include "TVPEvent.h"

#define NCB_MODULE_NAME TJS_N("sqlite3.dll")



// メッセージコード
#define	WM_SQLITE_STATECHANGE (WM_APP+8)
#define	WM_SQLITE_PROGRESS    (WM_APP+9)
#define PROGRESS_COUNT 100

/**
 * ステートメント tTJSVariant をバインド
 * @param stmt ステートメント
 * @param param バインドするデータ
 * @param pos バインド位置
 * @return エラーコード
 */
static int
bindParam(sqlite3_stmt *stmt, const tTJSVariant &param, int pos)
{
	switch (param.Type()) {
	case tvtInteger:
		return sqlite3_bind_int64(stmt, pos, (sqlite3_int64)param.AsInteger());
	case tvtReal:
		return sqlite3_bind_double(stmt, pos, param);
	case tvtString:
		{
			tTJSVariantString *str = param.AsStringNoAddRef();
			return sqlite3_bind_text(stmt, pos, *str, str->GetLength() * sizeof(tjs_char), SQLITE_TRANSIENT);
		}
	case tvtOctet:
		{
			tTJSVariantOctet *octet = param.AsOctetNoAddRef();
			return sqlite3_bind_blob(stmt, pos, octet->GetData(), octet->GetLength(), SQLITE_STATIC);
		}
	default:
		return sqlite3_bind_null(stmt, pos);
	}
}

/**
 * バインド位置を取得
 * @param name パラメータ名
 * @return バインド位置
 */
static int
getBindPos(sqlite3_stmt *stmt, const tTJSVariant &name)
{
	switch (name.Type()) {
	case tvtInteger:
	case tvtReal:
		return (int)name + 1;
	case tvtString:
		{
			int ret = 0;
			const tjs_char *buf = name.GetString();
			ret =sqlite3_bind_parameter_index(stmt, buf);
			return ret;
		}
	default:
		return 0;
	}
}

/**
 * バインド処理の呼び出し用
 */
class BindCaller : public tTJSDispatch /** EnumMembers 用 */
{
protected:
	sqlite3_stmt *stmt;
	int errorCode;
public:
	BindCaller(sqlite3_stmt *stmt) : stmt(stmt), errorCode(SQLITE_OK) {};
	int getErrorCode() {
		return errorCode;
	}
	virtual tjs_error FuncCall( // function invocation
												tjs_uint32 flag,			// calling flag
												const tjs_char * membername,// member name ( NULL for a default member )
												tjs_uint32 *hint,			// hint for the member name (in/out)
												tTJSVariant *result,		// result
												tjs_int numparams,			// number of parameters
												tTJSVariant **param,		// parameters
												iTJSDispatch2 *objthis		// object as "this"
												) {
		if (numparams > 1) {
			tTVInteger flag = param[1]->AsInteger();
			if (!(flag & TJS_HIDDENMEMBER)) {
				errorCode = bindParam(stmt, *param[1], getBindPos(stmt, *param[0]));
			}
		}
		if (result) {
			*result = errorCode == SQLITE_OK;
		}
		return TJS_S_OK;
	}
};

/**
 * 複数パラメータをバインド
 * @param stmt ステートメント
 * @param params 複数パラメータの配列または辞書
 * @return エラーコード
 */
static int
bindParams(sqlite3_stmt *stmt, const tTJSVariant &params)
{
	tTJSVariantClosure &vc = params.AsObjectClosureNoAddRef();
	if (vc.IsInstanceOf(TJS_IGNOREPROP,NULL,NULL, "Array",NULL) == TJS_S_TRUE) {
		int ret = SQLITE_OK;
		params.AsObjectClosureNoAddRef();
		tTJSVariant count;
		vc.PropGet(0, "count", NULL, &count, NULL);
		int cnt = count;
		for (int i=0;i<cnt;i++) {
			tTJSVariant value;
			vc.PropGetByNum(0, i, &value, NULL);
			if ((ret = bindParam(stmt, value, i+1)) != SQLITE_OK) {
				break;
			}
		}
		return ret;
	} else {
		BindCaller *caller = new BindCaller(stmt);
		tTJSVariantClosure closure(caller);
		vc.EnumMembers(TJS_IGNOREPROP, &closure, NULL);
		int errorCode = caller->getErrorCode();
		caller->Release();
		return errorCode;
	}
}

/**
 * ステートメントから tTJSVariant を取得
 * @param stmt ステートメント
 * @param variant 格納先
 * @param num カラム番号
 */
static void
getColumnData(sqlite3_stmt *stmt, tTJSVariant &variant, int num)
{
	switch (sqlite3_column_type(stmt, num)) {
	case SQLITE_INTEGER:
		variant = (tjs_int64)sqlite3_column_int64(stmt, num);
		break;
	case SQLITE_FLOAT:
		variant = sqlite3_column_double(stmt, num);
		break;
	case SQLITE_TEXT:
		variant = (const tjs_char *)sqlite3_column_text(stmt, num);
		break;
	case SQLITE_BLOB:
		variant = tTJSVariant((const tjs_uint8 *) sqlite3_column_blob(stmt, num), sqlite3_column_bytes(stmt, num));
		break;
	default:
		variant.Clear();
		break;
	}
}

extern void initContainFunc(sqlite3 *db);

/**
 * Sqliteクラス
 */
class Sqlite {

public:
	/**
	 * コンストラクタ
	 * @param database データベースファイル名
	 * @param readonly 読み込み専用
	 */
	Sqlite(const tjs_char *database, bool readonly=false) : db(NULL) {
		if (readonly) {
			// 読み込み専用時は吉里吉里の処理系を使う
			sqlite3_open_v2(database, &db, SQLITE_OPEN_READONLY, "xp3");
		} else {
			// そうでない場合はOSの処理系を使う
			ttstr filename = database;
			if (*database == '\0' || *database == ':') {
				// 空文字または ':' で始まる場合は sqlite の特殊な扱い
				sqlite3_open(database, &db);
			} else {
				filename = TVPNormalizeStorageName(filename);
				if (filename.length() && !std::strstr(filename.c_str(), (const tjs_char*)">")) {
					TVPGetLocalName(filename);
					sqlite3_open(filename.c_str(), &db);
				}
			}
		}
		if (db) {
			initContainFunc(db);
		}
	}

	/**
	 * デストラクタ
	 */
	~Sqlite() {
		if (db) {
			sqlite3_close(db);
		}
	}

	/**
	 * SQLを実行する
	 */
	static tjs_error exec(tTJSVariant *result, tjs_int numparams, tTJSVariant **params, Sqlite *self) {
		if (numparams < 1) {
			return TJS_E_BADPARAMCOUNT;
		}
		sqlite3_stmt *stmt = NULL;
		int ret;
		if ((ret = sqlite3_prepare_v2(self->db, params[0]->GetString(), -1, &stmt, NULL)) == SQLITE_OK) {
			sqlite3_reset(stmt);
			if (numparams <= 1 || (ret = ::bindParams(stmt, *params[1])) == SQLITE_OK) {
				if (numparams > 2 && params[2]->Type() == tvtObject) {
					tTJSVariantClosure &callback = params[2]->AsObjectClosureNoAddRef();
					// カラム数
					int argc = sqlite3_column_count(stmt);
					// 引数初期化
					tTJSVariant **args = new tTJSVariant*[argc];
					for (int i=0;i<argc;i++) {
						args[i] = new tTJSVariant();
					}
					while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
						// 引数に展開
						for (int i=0;i<argc;i++) {
							::getColumnData(stmt, *args[i], i);
						}
						callback.FuncCall(0, NULL, NULL, NULL, argc, args, NULL);
					}
					// 引数破棄
					for (int i=0;i<argc;i++) {
						delete args[i];
					}
					delete[] args;
				} else {
					while ((ret = sqlite3_step(stmt)) == SQLITE_ROW);
				}
			}
			sqlite3_finalize(stmt);
		}
		if (result) {
			*result = ret == SQLITE_OK || ret == SQLITE_DONE;
		}
		return TJS_S_OK;
	}

	/**
	 * 値取得用にSQLを実行する。
	 */
	static tjs_error execValue(tTJSVariant *result, tjs_int numparams, tTJSVariant **params, Sqlite *self) {
		if (numparams < 1) {
			return TJS_E_BADPARAMCOUNT;
		}
		sqlite3_stmt *stmt = NULL;
		if (sqlite3_prepare_v2(self->db, params[0]->GetString(), -1, &stmt, NULL) == SQLITE_OK) {
			sqlite3_reset(stmt);
			if (numparams <= 1 || ::bindParams(stmt, *params[1]) == SQLITE_OK) {
				int argc = sqlite3_column_count(stmt);
				if (sqlite3_step(stmt) == SQLITE_ROW && argc > 0) {
					if (result) {
						::getColumnData(stmt, *result, 0);
					}
				}
				sqlite3_finalize(stmt);
			}
		}
		return TJS_S_OK;
	}
	
	bool begin() {
		return sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL) == SQLITE_OK;
	}
	bool commit() {
		return sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) == SQLITE_OK;
	}
	bool rollback() {
		return sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL) == SQLITE_OK;
	}

	tjs_int64 getLastInsertRowId() const {
		return db ? sqlite3_last_insert_rowid(db) : 0;
	}

	int getErrorCode() const {
		return db ? sqlite3_errcode(db) : -1;
	}
	
	ttstr getErrorMessage() const {
		return db ? ttstr((const tjs_char*)sqlite3_errmsg(db)) : ttstr("database open failed");
	}
	
	/**
	 * インスタンス生成ファクトリ
	 */
	static tjs_error factory(Sqlite **result, tjs_int numparams, tTJSVariant **params, iTJSDispatch2 *objthis) {
		if (numparams < 1) {
			return TJS_E_BADPARAMCOUNT;
		}
		*result = new Sqlite(params[0]->GetString(), numparams > 1 ? (int)*params[1] != 0 : false);
		return TJS_S_OK;
	}
	
	sqlite3 *getDatabase() const {
		return db;
	}
	
private:
	sqlite3 *db;
};

#define ENUM(n) Variant(#n, (int)n)


NCB_REGISTER_CLASS(Sqlite) {
	ENUM(SQLITE_OK); /* Successful result */
	ENUM(SQLITE_ERROR); /* SQL error or missing database */
	ENUM(SQLITE_INTERNAL); /* Internal logic error in SQLite */
	ENUM(SQLITE_PERM); /* Access permission denied */
	ENUM(SQLITE_ABORT); /* Callback routine requested an abort */
	ENUM(SQLITE_BUSY); /* The database file is locked */
	ENUM(SQLITE_LOCKED); /* A table in the database is locked */
	ENUM(SQLITE_NOMEM); /* A malloc() failed */
	ENUM(SQLITE_READONLY); /* Attempt to write a readonly database */
	ENUM(SQLITE_INTERRUPT); /* Operation terminated by sqlite3_interrupt()*/
	ENUM(SQLITE_IOERR); /* Some kind of disk I/O error occurred */
	ENUM(SQLITE_CORRUPT); /* The database disk image is malformed */
	ENUM(SQLITE_NOTFOUND); /* NOT USED. Table or record not found */
	ENUM(SQLITE_FULL); /* Insertion failed because database is full */
	ENUM(SQLITE_CANTOPEN); /* Unable to open the database file */
	ENUM(SQLITE_PROTOCOL); /* NOT USED. Database lock protocol error */
	ENUM(SQLITE_EMPTY); /* Database is empty */
	ENUM(SQLITE_SCHEMA); /* The database schema changed */
	ENUM(SQLITE_TOOBIG); /* String or BLOB exceeds size limit */
	ENUM(SQLITE_CONSTRAINT); /* Abort due to constraint violation */
	ENUM(SQLITE_MISMATCH); /* Data type mismatch */
	ENUM(SQLITE_MISUSE); /* Library used incorrectly */
	ENUM(SQLITE_NOLFS); /* Uses OS features not supported on host */
	ENUM(SQLITE_AUTH); /* Authorization denied */
	ENUM(SQLITE_FORMAT); /* Auxiliary database format error */
	ENUM(SQLITE_RANGE); /* 2nd parameter to sqlite3_bind out of range */
	ENUM(SQLITE_NOTADB); /* File opened that is not a database file */
	ENUM(SQLITE_ROW); /* sqlite3_step() has another row ready */
	ENUM(SQLITE_DONE); /* sqlite3_step() has finished executing */
	Factory(&ClassT::factory);
	RawCallback(TJS_N("exec"), &Class::exec, 0);
	RawCallback(TJS_N("execValue"), &Class::execValue, 0);
	NCB_METHOD(begin);
	NCB_METHOD(commit);
	NCB_METHOD(rollback);
	NCB_PROPERTY_RO(lastInsertRowId, getLastInsertRowId);
	NCB_PROPERTY_RO(errorCode, getErrorCode);
	NCB_PROPERTY_RO(errorMessage, getErrorMessage);
};

/**
 * Sqliteのステートメントを扱うクラス
 */
class SqliteStatement {

public:
	/**
	 * コンストラクタ
	 */
	SqliteStatement(tTJSVariant &sqlite) : sqlite(sqlite), db(NULL), stmt(NULL) {
		Sqlite *sq = ncbInstanceAdaptor<Sqlite>::GetNativeInstance(sqlite.AsObjectNoAddRef());
		if (sq) {
			db = sq->getDatabase();
		}
	}

	/**
	 * デストラクタ
	 */
	~SqliteStatement() {
		close();
	}

	// ステートを開く
	static tjs_error open(tTJSVariant *result, tjs_int numparams, tTJSVariant **params, SqliteStatement *self) {
		if (numparams < 1) {
			return TJS_E_BADPARAMCOUNT;
		}
		int ret = self->_open(params[0]->GetString(), numparams > 0 ? params[1]: NULL);
		if (result) {
			*result = ret;
		}
		return TJS_S_OK;
	}

	// ステートを閉じる
	void close() {
		if (stmt) {
			sqlite3_finalize(stmt);
			stmt = NULL;
		}
	}

	// sql取得
	tTJSString getSql() const {
		ttstr ret = sqlite3_sql(stmt);
		return ret;
	}

	// バインド状態のリセット
	int reset() {
		bindPos = 1;
		return sqlite3_reset(stmt);
	}

	// パラメータのバインド
	int bind(tTJSVariant params) {
		return ::bindParams(stmt, params);
	}

	// 指定位置バインド
	static tjs_error bindAt(tTJSVariant *result, tjs_int numparams, tTJSVariant **params, SqliteStatement *self) {
		if (numparams < 1) {
			return TJS_E_BADPARAMCOUNT;
		}
		int ret = self->_bindAt(*params[0], numparams > 0 ? params[1] : NULL);
		if (result) {
			*result = ret;
		}
		return TJS_S_OK;
	}

	// 単純実行
	int exec() {
		int ret = sqlite3_step(stmt);
		if (ret != SQLITE_ROW) {
			reset();
		}
		return ret;
	}

	// ステップ実行
	bool step() {
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			return true;
		}
		reset();
		return false;
	}

	// データ数
	int getCount() const {
		return sqlite3_data_count(stmt);
	}

	// カラム数
	int getColumnCount() const {
		return sqlite3_column_count(stmt);
	}

	// 指定カラムが NULLか？
	bool isNull(tTJSVariant column) const {
		return sqlite3_column_type(stmt, getColumnNo(column)) == SQLITE_NULL;
	}

	// カラムの型
	int getType(tTJSVariant column) const {
		return sqlite3_column_type(stmt, getColumnNo(column));
	}

	// カラム名
	ttstr getName(tTJSVariant column) const {
		return (const tjs_char *)sqlite3_column_name(stmt, getColumnNo(column));
	}

	// 値の取得
	static tjs_error get(tTJSVariant *result, tjs_int numparams, tTJSVariant **params, SqliteStatement *self) {
		if (result) {
			if (numparams == 0) {
				int count = sqlite3_column_count(self->stmt);
				iTJSDispatch2 *line = TJSCreateArrayObject();
				for (int i=0;i<count;i++) {
					tTJSVariant col, *param = &col;
					::getColumnData(self->stmt, col, i);
					line->FuncCall(0, "add", NULL, NULL, 1, &param, line);
				}
				*result = tTJSVariant(line, line);
				line->Release();
			} else {
				int col = self->getColumnNo(*params[0]);
				if (sqlite3_column_type(self->stmt, col) == SQLITE_NULL) {
					if (numparams > 1) {
						*result = *params[1];
					} else {
						result->Clear();
					}
				} else {
					::getColumnData(self->stmt, *result, col);
				}
			}
		}
		return TJS_S_OK;
	}

	// missing method
	static tjs_error missing(tTJSVariant *result, tjs_int numparams, tTJSVariant **params, SqliteStatement *self) {
		if (numparams < 3) {return TJS_E_BADPARAMCOUNT;};
		bool ret = false;
		if (!(int)*params[0]) {
			int col = self->getColumnNo(*params[1]);
			if (col >= 0) {
				tTJSVariant value;
				::getColumnData(self->stmt, value, col);
				params[2]->AsObjectClosureNoAddRef().PropSet(0, NULL, NULL, &value, NULL);
				ret = true;
			}
		}
		if (result) {
			*result = ret;
		}
		return TJS_S_OK;
	}
	
	/**
	 * インスタンス生成ファクトリ
	 */
	static tjs_error factory(SqliteStatement **result, tjs_int numparams, tTJSVariant **params, iTJSDispatch2 *objthis) {
		if (numparams < 1) {
			return TJS_E_BADPARAMCOUNT;
		}
		if (params[0]->AsObjectClosureNoAddRef().IsInstanceOf(0, NULL, NULL, "Sqlite", NULL) != TJS_S_TRUE) {
			TVPThrowExceptionMessage("use Sqlite class Object");
		}
		SqliteStatement *state = new SqliteStatement(*params[0]);
		if (numparams > 1) {
			if (state->_open(params[1]->GetString(), numparams > 2 ? params[2] : NULL) != SQLITE_OK) {
				delete state;
				TVPThrowExceptionMessage("failed to open state");
				return TJS_E_FAIL;
			}
		}
		tTJSVariant name(TJS_N("missing"));
		objthis->ClassInstanceInfo(TJS_CII_SET_MISSING, 0, &name);
		*result = state;
		return TJS_S_OK;
	}
	
protected:

	/**
	 * カラム番号を取得
	 * @param column カラム指定
	 * @return カラム番号
	 */
	int getColumnNo(const tTJSVariant &column) const {
		switch (column.Type()) {
		case tvtInteger:
		case tvtReal:
			return column;
		case tvtString:
			{
				const tjs_char *col = column.GetString();
				int count = sqlite3_column_count(stmt);
				for (int i=0; i<count; i++) {
					const char *col_name = sqlite3_column_name(stmt, i);
					if (col_name && SDL_strcasecmp(col, col_name) == 0) {
						return i;
					}
				}
			}
		default:
			return -1;
		}
	}

	/**
	 * sql を開く
	 * @param sql ステートとして開くsql
	 * @param params パラメータ
	 * @return エラーコード
	 */
	int _open(const tjs_char *sql, const tTJSVariant *params = NULL) {
		close();
		int ret;
		if ((ret = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL)) == SQLITE_OK) {
			reset();
			if (params) {
				ret = ::bindParams(stmt, *params);
			}
		}
		return ret;
	}

	// 指定位置バインド
	int _bindAt(tTJSVariant &value, tTJSVariant *pos=NULL) {
		if (pos) {
			bindPos = ::getBindPos(stmt, *pos);
		}
		return ::bindParam(stmt, value, bindPos++);
	}
	
private:
	tTJSVariant sqlite;
	sqlite3 *db;
	sqlite3_stmt *stmt;
	int bindPos;
};

NCB_REGISTER_CLASS(SqliteStatement) {
	ENUM(SQLITE_INTEGER);
	ENUM(SQLITE_FLOAT);
	ENUM(SQLITE_TEXT);
	ENUM(SQLITE_BLOB);
	ENUM(SQLITE_NULL);
	Factory(&ClassT::factory);
	RawCallback(TJS_N("open"), &Class::open, 0);
	NCB_METHOD(close);
	NCB_PROPERTY_RO(sql, getSql);
	NCB_METHOD(reset);
	NCB_METHOD(bind);
	RawCallback(TJS_N("bindAt"), &Class::bindAt, 0);
	NCB_METHOD(exec);
	NCB_METHOD(step);
	NCB_PROPERTY_RO(count, getCount);
	NCB_PROPERTY_RO(columnCount, getColumnCount);
	NCB_METHOD(isNull);
	NCB_METHOD(getType);
	NCB_METHOD(getName);
	RawCallback(TJS_N("get"), &Class::get, 0);
	RawCallback(TJS_N("missing"), &Class::missing, 0);
};

// --------------------------------------------------------------------------

extern void initNormalize();

static void
initSqlite()
{
	sqlite3_vfs_register(getXp3Vfs(), 0);
	initNormalize();
}

static void
doneSqlite()
{
	sqlite3_vfs_unregister(getXp3Vfs());
}

NCB_PRE_REGIST_CALLBACK(initSqlite);
NCB_POST_UNREGIST_CALLBACK(doneSqlite);
