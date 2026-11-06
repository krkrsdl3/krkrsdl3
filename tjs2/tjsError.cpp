//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TJS2's C++ exception class and exception message
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "tjsScriptBlock.h"
#include "tjsError.h"
#include "tjs.h"
#include "tjsUtils.h"
#include "tjsInterCodeGen.h"

#define TJS_MAX_TRACE_TEXT_LEN 1500

namespace TJS
{

//---------------------------------------------------------------------------
// TJSGetExceptionObject : retrieves TJS 'Exception' object
//---------------------------------------------------------------------------
void TJSGetExceptionObject(tTJS* tjs,
                           tTJSVariant* res,
                           tTJSVariant& msg,
                           tTJSVariant* trace /* trace is optional */)
{
    if (!res)
        return; // not prcess

    // retrieve class "Exception" from global
    iTJSDispatch2* global = tjs->GetGlobalNoAddRef();
    tTJSVariant val;
    static tTJSString Exception_name(TJS_N("Exception"));
    tjs_error hr =
        global->PropGet(0, Exception_name.c_str(), Exception_name.GetHint(), &val, global);
    if (TJS_FAILED(hr))
        TJS_eTJSError(TJSExceptionNotFound);
    // create an Exception object
    iTJSDispatch2* excpobj;
    tTJSVariantClosure clo = val.AsObjectClosureNoAddRef();
    tTJSVariant* pmsg = &msg;
    hr = clo.CreateNew(0, NULL, NULL, &excpobj, 1, &pmsg, clo.ObjThis);
    if (TJS_FAILED(hr))
        TJS_eTJSError(TJSExceptionNotFound);
    if (trace)
    {
        static tTJSString trace_name(TJS_N("trace"));
        excpobj->PropSet(TJS_MEMBERENSURE, trace_name.c_str(), trace_name.GetHint(), trace,
                         excpobj);
    }
    *res = tTJSVariant(excpobj, excpobj);
    excpobj->Release();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// eTJSScriptException
//---------------------------------------------------------------------------
eTJSScriptError::tScriptBlockHolder::tScriptBlockHolder(tTJSScriptBlock* block)
{
    Block = block;
    Block->AddRef();
}
//---------------------------------------------------------------------------
eTJSScriptError::tScriptBlockHolder::~tScriptBlockHolder()
{
    Block->Release();
}
//---------------------------------------------------------------------------
eTJSScriptError::tScriptBlockHolder::tScriptBlockHolder(const tScriptBlockHolder& Holder)
{
    Block = Holder.Block;
    Block->AddRef();
}
//---------------------------------------------------------------------------
eTJSScriptError::eTJSScriptError(const ttstr& Msg, tTJSScriptBlock* block, tjs_int pos)
  : eTJSError(Msg),
    Block(block),
    Position(pos)
{
}
//---------------------------------------------------------------------------
tjs_int eTJSScriptError::GetSourceLine() const
{
    return Block.Block->SrcPosToLine(Position) + 1;
}
//---------------------------------------------------------------------------
const tjs_char* eTJSScriptError::GetBlockName() const
{
    const tjs_char* name = Block.Block->GetName();
    return name ? name : TJS_N("");
}
//---------------------------------------------------------------------------
bool eTJSScriptError::AddTrace(tTJSScriptBlock* block, tjs_int srcpos)
{
    tjs_int len = Trace.GetLen();
    if (len >= TJS_MAX_TRACE_TEXT_LEN)
        return false;

    if (len != 0)
        Trace += TJS_N("\n<-- ");
    Trace += block->GetLineDescriptionString(srcpos);

    return true;
}
//---------------------------------------------------------------------------
bool eTJSScriptError::AddTrace(tTJSInterCodeContext* context, tjs_int codepos)
{
    tjs_int len = Trace.GetLen();
    if (len >= TJS_MAX_TRACE_TEXT_LEN)
        return false;

    if (len != 0)
        Trace += TJS_N(" <-- ");
    Trace += context->GetPositionDescriptionString(codepos);

    return true;
}
//---------------------------------------------------------------------------
bool eTJSScriptError::AddTrace(const ttstr& data)
{
    tjs_int len = Trace.GetLen();
    if (len >= TJS_MAX_TRACE_TEXT_LEN)
        return false;
    if (len != 0)
        Trace += TJS_N(" <-- ");
    Trace += data;
    return true;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// throw helper functions
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
static void TJSReportExceptionSource(const ttstr& msg, tTJSScriptBlock* block, tjs_int srcpos)
{
    if (TJSEnableDebugMode)
    {
        tTJS* tjs = block->GetTJS();
        tjs->OutputExceptionToConsole(
            (msg + TJS_N(" at ") + block->GetLineDescriptionString(srcpos)).c_str());
    }
}
//---------------------------------------------------------------------------
static void TJSReportExceptionSource(const ttstr& msg,
                                     tTJSInterCodeContext* context,
                                     tjs_int codepos)
{
    if (TJSEnableDebugMode)
    {
        tTJS* tjs = context->GetBlock()->GetTJS();
        tjs->OutputExceptionToConsole(
            (msg + TJS_N(" at ") + context->GetPositionDescriptionString(codepos)).c_str());
    }
}
//---------------------------------------------------------------------------
void TJS_eTJS()
{
    throw eTJS();
}
//---------------------------------------------------------------------------
void TJS_eTJSError(const ttstr& msg)
{
    throw eTJSError(msg);
}
void TJS_eTJSError(const tjs_char* msg)
{
    throw eTJSError(msg);
}
//---------------------------------------------------------------------------
void TJS_eTJSVariantError(const ttstr& msg)
{
    throw eTJSVariantError(msg);
}
void TJS_eTJSVariantError(const tjs_char* msg)
{
    throw eTJSVariantError(msg);
}
//---------------------------------------------------------------------------
void TJS_eTJSScriptError(const ttstr& msg, tTJSScriptBlock* block, tjs_int srcpos)
{
    TJSReportExceptionSource(msg, block, srcpos);
    throw eTJSScriptError(msg, block, srcpos);
}
//---------------------------------------------------------------------------
void TJS_eTJSScriptError(const tjs_char* msg, tTJSScriptBlock* block, tjs_int srcpos)
{
    TJSReportExceptionSource(msg, block, srcpos);
    throw eTJSScriptError(msg, block, srcpos);
}
//---------------------------------------------------------------------------
void TJS_eTJSScriptError(const ttstr& msg, tTJSInterCodeContext* context, tjs_int codepos)
{
    TJSReportExceptionSource(msg, context, codepos);
    throw eTJSScriptError(msg, context->GetBlock(), context->CodePosToSrcPos(codepos));
}
//---------------------------------------------------------------------------
void TJS_eTJSScriptError(const tjs_char* msg, tTJSInterCodeContext* context, tjs_int codepos)
{
    TJSReportExceptionSource(msg, context, codepos);
    throw eTJSScriptError(msg, context->GetBlock(), context->CodePosToSrcPos(codepos));
}
//---------------------------------------------------------------------------
void TJS_eTJSScriptException(const ttstr& msg,
                             tTJSScriptBlock* block,
                             tjs_int srcpos,
                             tTJSVariant& val)
{
    TJSReportExceptionSource(msg, block, srcpos);
    throw eTJSScriptException(msg, block, srcpos, val);
}
//---------------------------------------------------------------------------
void TJS_eTJSScriptException(const tjs_char* msg,
                             tTJSScriptBlock* block,
                             tjs_int srcpos,
                             tTJSVariant& val)
{
    TJSReportExceptionSource(msg, block, srcpos);
    throw eTJSScriptException(msg, block, srcpos, val);
}
//---------------------------------------------------------------------------
void TJS_eTJSScriptException(const ttstr& msg,
                             tTJSInterCodeContext* context,
                             tjs_int codepos,
                             tTJSVariant& val)
{
    TJSReportExceptionSource(msg, context, codepos);
    throw eTJSScriptException(msg, context->GetBlock(), context->CodePosToSrcPos(codepos), val);
}
//---------------------------------------------------------------------------
void TJS_eTJSScriptException(const tjs_char* msg,
                             tTJSInterCodeContext* context,
                             tjs_int codepos,
                             tTJSVariant& val)
{
    TJSReportExceptionSource(msg, context, codepos);
    throw eTJSScriptException(msg, context->GetBlock(), context->CodePosToSrcPos(codepos), val);
}
//---------------------------------------------------------------------------
void TJS_eTJSCompileError(const ttstr& msg, tTJSScriptBlock* block, tjs_int srcpos)
{
    TJSReportExceptionSource(msg, block, srcpos);
    throw eTJSCompileError(msg, block, srcpos);
}
//---------------------------------------------------------------------------
void TJS_eTJSCompileError(const tjs_char* msg, tTJSScriptBlock* block, tjs_int srcpos)
{
    TJSReportExceptionSource(msg, block, srcpos);
    throw eTJSCompileError(msg, block, srcpos);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void TJSThrowFrom_tjs_error(tjs_error hr, const tjs_char* name)
{
    // raise an exception descripted as tjs_error
    // name = variable name ( otherwide it can be NULL )

    switch (hr)
    {
        case TJS_E_MEMBERNOTFOUND:
        {
            if (name)
            {
                ttstr str(TJSMemberNotFound);
                str.Replace(TJS_N("%1"), name);
                TJS_eTJSError(str);
            }
            else
            {
                TJS_eTJSError(TJSMemberNotFoundNoNameGiven);
            }
        }
        case TJS_E_NOTIMPL:
            TJS_eTJSError(TJSNotImplemented);
        case TJS_E_INVALIDPARAM:
            TJS_eTJSError(TJSInvalidParam);
        case TJS_E_BADPARAMCOUNT:
            TJS_eTJSError(TJSBadParamCount);
        case TJS_E_INVALIDTYPE:
            TJS_eTJSError(TJSInvalidType);
        case TJS_E_ACCESSDENYED:
            TJS_eTJSError(TJSAccessDenyed);
        case TJS_E_INVALIDOBJECT:
            TJS_eTJSError(TJSInvalidObject);
        case TJS_E_NATIVECLASSCRASH:
            TJS_eTJSError(TJSNativeClassCrash);
        default:
            if (TJS_FAILED(hr))
            {
                tjs_char buf[256];
                TJS_snprintf(buf, 256, TJSUnknownFailure, hr);
                TJS_eTJSError(buf);
            }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// pending error state
//---------------------------------------------------------------------------
tTJSPendingErrorState::tTJSPendingErrorState() : Kind(tpekNone), Block(NULL), Position(0)
{
}
//---------------------------------------------------------------------------
tTJSPendingErrorState::~tTJSPendingErrorState()
{
    Clear();
}
//---------------------------------------------------------------------------
void tTJSPendingErrorState::Clear()
{
    Kind = tpekNone;
    Message.Clear();
    Trace.Clear();
    Value.Clear();
    if (Block)
    {
        Block->Release();
        Block = NULL;
    }
    Position = 0;
    Unknown = std::exception_ptr();
}
//---------------------------------------------------------------------------
thread_local tTJSPendingErrorState* TJS_PENDING_ERROR = NULL;
//---------------------------------------------------------------------------
tTJSPendingErrorState* TJSGetPendingError()
{
    // intentionally never freed: thread local destruction order may outlive
    // the engine's object/string heaps
    if (!TJS_PENDING_ERROR)
        TJS_PENDING_ERROR = new tTJSPendingErrorState();
    return TJS_PENDING_ERROR;
}
//---------------------------------------------------------------------------
void TJSSetPendingError(const ttstr& msg)
{
    tTJSPendingErrorState* e = TJSGetPendingError();
    if (e->Kind != tpekNone)
        return;
    e->Clear();
    e->Kind = tpekPlain;
    e->Message = msg;
}
//---------------------------------------------------------------------------
void TJSSetPendingError(const tjs_char* msg)
{
    TJSSetPendingError(ttstr(msg));
}
//---------------------------------------------------------------------------
void TJSSetPendingFrom_tjs_error(tjs_error hr, const tjs_char* name)
{
    switch (hr)
    {
        case TJS_E_MEMBERNOTFOUND:
        {
            if (name)
            {
                ttstr str(TJSMemberNotFound);
                str.Replace(TJS_N("%1"), name);
                TJSSetPendingError(str);
            }
            else
            {
                TJSSetPendingError(TJSMemberNotFoundNoNameGiven);
            }
            return;
        }
        case TJS_E_NOTIMPL:
            TJSSetPendingError(TJSNotImplemented);
            return;
        case TJS_E_INVALIDPARAM:
            TJSSetPendingError(TJSInvalidParam);
            return;
        case TJS_E_BADPARAMCOUNT:
            TJSSetPendingError(TJSBadParamCount);
            return;
        case TJS_E_INVALIDTYPE:
            TJSSetPendingError(TJSInvalidType);
            return;
        case TJS_E_ACCESSDENYED:
            TJSSetPendingError(TJSAccessDenyed);
            return;
        case TJS_E_INVALIDOBJECT:
            TJSSetPendingError(TJSInvalidObject);
            return;
        case TJS_E_NATIVECLASSCRASH:
            TJSSetPendingError(TJSNativeClassCrash);
            return;
        default:
            if (TJS_FAILED(hr))
            {
                tjs_char buf[256];
                TJS_snprintf(buf, 256, TJSUnknownFailure, hr);
                TJSSetPendingError(buf);
            }
            return;
    }
}
//---------------------------------------------------------------------------
void TJSSetPendingVariantConvertError(const tTJSVariant& from, tTJSVariantType to)
{
    ttstr msg;
    if (to == tvtObject)
    {
        msg = TJSVariantConvertErrorToObject;
        msg.Replace(TJS_N("%1"), TJSVariantToReadableString(from));
    }
    else
    {
        msg = TJSVariantConvertError;
        msg.Replace(TJS_N("%1"), TJSVariantToReadableString(from));
        msg.Replace(TJS_N("%2"), TJSVariantTypeToTypeString(to));
    }
    TJSSetPendingError(msg);
}
//---------------------------------------------------------------------------
void TJSSetPendingVariantConvertError(const tTJSVariant& from,
                                      tTJSVariantType to1,
                                      tTJSVariantType to2)
{
    ttstr msg(TJSVariantConvertError);
    msg.Replace(TJS_N("%1"), TJSVariantToReadableString(from));
    msg.Replace(TJS_N("%2"), TJSVariantTypeToTypeString(to1) + ttstr(TJS_N("/")) +
                                 TJSVariantTypeToTypeString(to2));
    TJSSetPendingError(msg);
}
//---------------------------------------------------------------------------
void TJSSetPendingSilent()
{
    tTJSPendingErrorState* e = TJSGetPendingError();
    if (e->Kind != tpekNone)
        return;
    e->Clear();
    e->Kind = tpekSilent;
}
//---------------------------------------------------------------------------
void TJSSetPendingScriptError(const ttstr& msg, tTJSScriptBlock* block, tjs_int srcpos)
{
    tTJSPendingErrorState* e = TJSGetPendingError();
    if (e->Kind != tpekNone)
        return;
    e->Clear();
    e->Kind = tpekScriptError;
    e->Message = msg;
    e->Block = block;
    if (block)
        block->AddRef();
    e->Position = srcpos;
}
//---------------------------------------------------------------------------
void TJSSetPendingScriptException(const ttstr& msg,
                                  tTJSScriptBlock* block,
                                  tjs_int srcpos,
                                  tTJSVariant& val)
{
    tTJSPendingErrorState* e = TJSGetPendingError();
    if (e->Kind != tpekNone)
        return;
    e->Clear();
    e->Kind = tpekScriptException;
    e->Message = msg;
    e->Block = block;
    if (block)
        block->AddRef();
    e->Position = srcpos;
    e->Value = val;
}
//---------------------------------------------------------------------------
void TJSAdoptScriptError(eTJSScriptError& e)
{
    tTJSPendingErrorState* p = TJSGetPendingError();
    if (p->Kind != tpekNone)
        return;
    p->Clear();
    p->Kind = tpekScriptError;
    p->Message = e.GetMessage();
    p->Trace = e.GetTrace();
    p->Block = e.GetBlockNoAddRef();
    if (p->Block)
        p->Block->AddRef();
    p->Position = e.GetPosition();
}
//---------------------------------------------------------------------------
void TJSAdoptScriptException(eTJSScriptException& e)
{
    tTJSPendingErrorState* p = TJSGetPendingError();
    if (p->Kind != tpekNone)
        return;
    p->Clear();
    p->Kind = tpekScriptException;
    p->Message = e.GetMessage();
    p->Trace = e.GetTrace();
    p->Block = e.GetBlockNoAddRef();
    if (p->Block)
        p->Block->AddRef();
    p->Position = e.GetPosition();
    p->Value = e.GetValue();
}
//---------------------------------------------------------------------------
void TJSAdoptPlainError(const ttstr& msg)
{
    tTJSPendingErrorState* p = TJSGetPendingError();
    if (p->Kind != tpekNone)
        return;
    p->Clear();
    p->Kind = tpekPlain;
    p->Message = msg;
}
//---------------------------------------------------------------------------
void TJSAdoptCurrentException()
{
    // must be called from inside a catch handler
    try
    {
        throw;
    }
    catch (eTJSSilent&)
    {
        TJSSetPendingSilent();
    }
    catch (eTJSScriptException& e)
    {
        TJSAdoptScriptException(e);
    }
    catch (eTJSScriptError& e)
    {
        TJSAdoptScriptError(e);
    }
    catch (eTJS& e)
    {
        TJSAdoptPlainError(e.GetMessage());
    }
    catch (std::exception& e)
    {
        TJSAdoptPlainError(ttstr(e.what()));
    }
    catch (const char* text)
    {
        TJSAdoptPlainError(ttstr(text));
    }
    catch (...)
    {
        tTJSPendingErrorState* p = TJSGetPendingError();
        if (p->Kind == tpekNone)
        {
            p->Clear();
            p->Kind = tpekUnknown;
            p->Unknown = std::current_exception();
        }
    }
}
//---------------------------------------------------------------------------
bool TJSPendingAddTrace(tTJSScriptBlock* block, tjs_int srcpos)
{
    tTJSPendingErrorState* e = TJSGetPendingError();
    if (e->Kind == tpekNone)
        return false;
    tjs_int len = e->Trace.GetLen();
    if (len >= TJS_MAX_TRACE_TEXT_LEN)
        return false;
    if (len != 0)
        e->Trace += TJS_N("\n<-- ");
    e->Trace += block->GetLineDescriptionString(srcpos);
    return true;
}
//---------------------------------------------------------------------------
bool TJSPendingAddTrace(tTJSInterCodeContext* context, tjs_int codepos)
{
    tTJSPendingErrorState* e = TJSGetPendingError();
    if (e->Kind == tpekNone)
        return false;
    tjs_int len = e->Trace.GetLen();
    if (len >= TJS_MAX_TRACE_TEXT_LEN)
        return false;
    if (len != 0)
        e->Trace += TJS_N(" <-- ");
    e->Trace += context->GetPositionDescriptionString(codepos);
    return true;
}
//---------------------------------------------------------------------------
bool TJSPendingAddTrace(const ttstr& data)
{
    tTJSPendingErrorState* e = TJSGetPendingError();
    if (e->Kind == tpekNone)
        return false;
    tjs_int len = e->Trace.GetLen();
    if (len >= TJS_MAX_TRACE_TEXT_LEN)
        return false;
    if (len != 0)
        e->Trace += TJS_N(" <-- ");
    e->Trace += data;
    return true;
}
//---------------------------------------------------------------------------
void TJSPromotePendingToScriptError(tTJSInterCodeContext* context, tjs_int codepos)
{
    tTJSPendingErrorState* e = TJSGetPendingError();
    if (e->Kind != tpekPlain)
        return;

    if (TJSEnableDebugMode)
    {
        tTJS* tjs = context->GetBlock()->GetTJS();
        tjs->OutputExceptionToConsole(
            (e->Message + TJS_N(" at ") + context->GetPositionDescriptionString(codepos)).c_str());
    }

    e->Kind = tpekScriptError;
    e->Block = context->GetBlock();
    e->Block->AddRef();
    e->Position = context->CodePosToSrcPos(codepos);
}
//---------------------------------------------------------------------------
bool TJSBuildPendingExceptionObject(tTJS* tjs, tTJSVariant* result)
{
    tTJSPendingErrorState* e = TJSGetPendingError();
    if (e->Kind == tpekNone || e->Kind == tpekSilent || e->Kind == tpekUnknown)
        return false;
    if (!result)
        return true;

    if (e->Kind == tpekScriptException)
    {
        *result = e->Value;
        return true;
    }

    ttstr msg = e->Message;
    ttstr trace = e->Trace;

    // the Exception constructor can execute script code; do not let an
    // in-flight pending error leak into (or be overwritten by) that call
    e->Clear();

    try
    {
        tTJSVariant msgv(msg);
        tTJSVariant tracev(trace);
        TJSGetExceptionObject(tjs, result, msgv, &tracev);
        return true;
    }
    catch (...)
    {
        TJSAdoptCurrentException();
        return false;
    }
}
//---------------------------------------------------------------------------
void TJSThrowPendingError()
{
    tTJSPendingErrorState* e = TJSGetPendingError();
    if (e->Kind == tpekNone)
        return;

    switch (e->Kind)
    {
        case tpekSilent:
        {
            e->Clear();
            throw eTJSSilent();
        }
        case tpekScriptException:
        {
            if (!e->Block)
            {
                ttstr msg = e->Message;
                e->Clear();
                throw eTJSError(msg);
            }
            eTJSScriptException ex(e->Message, e->Block, e->Position, e->Value);
            if (!e->Trace.IsEmpty())
                ex.AddTrace(e->Trace);
            e->Clear();
            throw ex;
        }
        case tpekScriptError:
        {
            if (!e->Block)
            {
                ttstr msg = e->Message;
                e->Clear();
                throw eTJSError(msg);
            }
            eTJSScriptError ex(e->Message, e->Block, e->Position);
            if (!e->Trace.IsEmpty())
                ex.AddTrace(e->Trace);
            e->Clear();
            throw ex;
        }
        case tpekUnknown:
        {
            std::exception_ptr unknown = e->Unknown;
            e->Clear();
            std::rethrow_exception(unknown);
        }
        default:
        {
            ttstr msg = e->Message;
            e->Clear();
            throw eTJSError(msg);
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// error messages  ( can be localized )
//---------------------------------------------------------------------------
ttstr TJSNonamedException = TJS_N("No-named exception");
//---------------------------------------------------------------------------
} // namespace TJS

#define TJS_DECL_MESSAGE_BODY
#undef tjsErrorH
#undef __TJS_ERROR_INC_H__
#include "tjsError.h"
//---------------------------------------------------------------------------
