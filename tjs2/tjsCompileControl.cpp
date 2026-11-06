//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Conditional Compile Control
//---------------------------------------------------------------------------

#include "tjsCommHead.h"

#include "tjsCompileControl.h"
#include "tjsLex.h"
#include "tjsVariant.h"
#include "tjsError.h"

namespace TJS
{
// Token set of the conditional-compile expression language.  The values are
// those the old bison grammar used; GetNext below returns them.
enum
{
    PT_LPARENTHESIS = 258,
    PT_RPARENTHESIS = 259,
    PT_ERROR = 260,
    PT_COMMA = 261,
    PT_EQUAL = 262,
    PT_NOTEQUAL = 263,
    PT_EQUALEQUAL = 264,
    PT_LOGICALOR = 265,
    PT_LOGICALAND = 266,
    PT_VERTLINE = 267,
    PT_CHEVRON = 268,
    PT_AMPERSAND = 269,
    PT_LT = 270,
    PT_GT = 271,
    PT_LTOREQUAL = 272,
    PT_GTOREQUAL = 273,
    PT_PLUS = 274,
    PT_MINUS = 275,
    PT_ASTERISK = 276,
    PT_SLASH = 277,
    PT_PERCENT = 278,
    PT_EXCLAMATION = 279,
    PT_UN = 280,
    PT_SYMBOL = 281,
    PT_NUM = 282
};
}

namespace TJS
{
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
namespace
{
//---------------------------------------------------------------------------
// Conditional-compile expression evaluator.
//
// Precedence, weakest first: ',', '=', '||', '&&', '|', '^', '&', '==' '!=',
// '<' '>' '<=' '>=', '+' '-', '*' '/' '%', unary '!' '+' '-'.  All binary
// operators are left associative.  An assignment may only have a plain
// identifier on its left and is right associative.
//---------------------------------------------------------------------------
class tTJSPPExprEval
{
public:
    explicit tTJSPPExprEval(tTJSPPExprParser* parser) : P(parser), T(parser->GetTJS()), Tok(0), Val(0)
    {
        Next();
    }

    bool Run(tjs_int32& result)
    {
        if (!ParseAt(0, result))
            return false;
        return Tok == 0;
    }

private:
    tTJSPPExprParser* P;
    tTJS* T;
    int Tok;
    tjs_int32 Val;

    void Next() { Tok = P->GetNext(Val); }

    static int Precedence(int tok)
    {
        switch (tok)
        {
            case PT_COMMA:
                return 1;
            case PT_LOGICALOR:
                return 3;
            case PT_LOGICALAND:
                return 4;
            case PT_VERTLINE:
                return 5;
            case PT_CHEVRON:
                return 6;
            case PT_AMPERSAND:
                return 7;
            case PT_EQUALEQUAL:
            case PT_NOTEQUAL:
                return 8;
            case PT_LT:
            case PT_GT:
            case PT_LTOREQUAL:
            case PT_GTOREQUAL:
                return 9;
            case PT_PLUS:
            case PT_MINUS:
                return 10;
            case PT_ASTERISK:
            case PT_SLASH:
            case PT_PERCENT:
                return 11;
            default:
                return 0;
        }
    }

    bool ParseUnary(tjs_int32& out)
    {
        int op = Tok;
        if (op == PT_EXCLAMATION || op == PT_PLUS || op == PT_MINUS)
        {
            Next();
            tjs_int32 value;
            if (!ParseUnary(value))
                return false;
            if (op == PT_EXCLAMATION)
                out = value ? 0 : 1;
            else if (op == PT_PLUS)
                out = value;
            else
                out = (tjs_int32)(0u - (tjs_uint32)value);
            return true;
        }

        if (Tok == PT_NUM)
        {
            out = Val;
            Next();
            return true;
        }

        if (Tok == PT_SYMBOL)
        {
            tjs_int32 id = Val;
            Next();
            if (Tok == PT_EQUAL)
            {
                Next();
                tjs_int32 value;
                if (!ParseAt(2, value))
                    return false;
                T->SetPPValue(P->GetString(id), value);
                out = value;
                return true;
            }
            out = T->GetPPValue(P->GetString(id));
            return true;
        }

        if (Tok == PT_LPARENTHESIS)
        {
            Next();
            if (!ParseAt(0, out))
                return false;
            if (Tok != PT_RPARENTHESIS)
                return false;
            Next();
            return true;
        }

        return false;
    }

    bool ParseAt(int min_precedence, tjs_int32& out)
    {
        tjs_int32 left;
        if (!ParseUnary(left))
            return false;

        for (;;)
        {
            int op = Tok;
            int precedence = Precedence(op);
            if (precedence == 0 || precedence < min_precedence)
                break;

            Next();
            tjs_int32 right;
            if (!ParseAt(precedence + 1, right))
                return false;

            tjs_uint32 a = (tjs_uint32)left;
            tjs_uint32 b = (tjs_uint32)right;
            switch (op)
            {
                case PT_COMMA:
                    left = right;
                    break;
                case PT_LOGICALOR:
                    left = (left || right) ? 1 : 0;
                    break;
                case PT_LOGICALAND:
                    left = (left && right) ? 1 : 0;
                    break;
                case PT_VERTLINE:
                    left = (tjs_int32)(a | b);
                    break;
                case PT_CHEVRON:
                    left = (tjs_int32)(a ^ b);
                    break;
                case PT_AMPERSAND:
                    left = (tjs_int32)(a & b);
                    break;
                case PT_EQUALEQUAL:
                    left = left == right ? 1 : 0;
                    break;
                case PT_NOTEQUAL:
                    left = left != right ? 1 : 0;
                    break;
                case PT_LT:
                    left = left < right ? 1 : 0;
                    break;
                case PT_GT:
                    left = left > right ? 1 : 0;
                    break;
                case PT_LTOREQUAL:
                    left = left <= right ? 1 : 0;
                    break;
                case PT_GTOREQUAL:
                    left = left >= right ? 1 : 0;
                    break;
                case PT_PLUS:
                    left = (tjs_int32)(a + b);
                    break;
                case PT_MINUS:
                    left = (tjs_int32)(a - b);
                    break;
                case PT_ASTERISK:
                    left = (tjs_int32)(a * b);
                    break;
                case PT_SLASH:
                    if (right == 0 || (left == (tjs_int32)0x80000000 && right == -1))
                        return false;
                    left = left / right;
                    break;
                case PT_PERCENT:
                    if (right == 0 || (left == (tjs_int32)0x80000000 && right == -1))
                        return false;
                    left = left % right;
                    break;
                default:
                    return false;
            }
        }

        out = left;
        return true;
    }
};
//---------------------------------------------------------------------------
} // namespace

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// tTJSPPExprParser
//---------------------------------------------------------------------------
tTJSPPExprParser::tTJSPPExprParser(tTJS* tjs, const tjs_char* script)
{
    // script pointed by "script" argument will be released by this class
    // via delete[]
    TJS = tjs;
    Script = script;
}
//---------------------------------------------------------------------------
tTJSPPExprParser::~tTJSPPExprParser()
{
    delete[] Script;
}
//---------------------------------------------------------------------------
tjs_int32 tTJSPPExprParser::Parse()
{
    Current = Script;
    Result = 0;
    tTJSPPExprEval eval(this);
    tjs_int32 result;
    if (!eval.Run(result))
    {
        TJS_eTJSError(TJSPPError);
    }
    Result = result;
    return Result;
}
//---------------------------------------------------------------------------
tjs_int tTJSPPExprParser::GetNext(tjs_int32& value)
{
    // get next token

    while (TJS_iswspace(Current) && *Current)
        Current++;
    if (!*Current)
        return 0;

    switch (*Current)
    {
        case TJS_N('('):
            Current++;
            return PT_LPARENTHESIS;

        case TJS_N(')'):
            Current++;
            return PT_RPARENTHESIS;

        case TJS_N(','):
            Current++;
            return PT_COMMA;

        case TJS_N('='):
            if (*(Current + 1) == TJS_N('='))
            {
                Current += 2;
                return PT_EQUALEQUAL;
            }
            Current++;
            return PT_EQUAL;

        case TJS_N('!'):
            if (*(Current + 1) == TJS_N('='))
            {
                Current += 2;
                return PT_NOTEQUAL;
            }
            Current++;
            return PT_EXCLAMATION;

        case TJS_N('|'):
            if (*(Current + 1) == TJS_N('|'))
            {
                Current += 2;
                return PT_LOGICALOR;
            }
            Current++;
            return PT_VERTLINE;

        case TJS_N('&'):
            if (*(Current + 1) == TJS_N('&'))
            {
                Current += 2;
                return PT_LOGICALAND;
            }
            Current++;
            return PT_AMPERSAND;

        case TJS_N('^'):
            Current++;
            return PT_CHEVRON;

        case TJS_N('+'):
            Current++;
            return PT_PLUS;

        case TJS_N('-'):
            Current++;
            return PT_MINUS;

        case TJS_N('*'):
            Current++;
            return PT_ASTERISK;

        case TJS_N('/'):
            Current++;
            return PT_SLASH;

        case TJS_N('%'):
            Current++;
            return PT_PERCENT;

        case TJS_N('<'):
            if (*(Current + 1) == TJS_N('='))
            {
                Current += 2;
                return PT_LTOREQUAL;
            }
            Current++;
            return PT_LT;

        case TJS_N('>'):
            if (*(Current + 1) == TJS_N('='))
            {
                Current += 2;
                return PT_GTOREQUAL;
            }
            Current++;
            return PT_GT;

        case TJS_N('0'):
        case TJS_N('1'):
        case TJS_N('2'):
        case TJS_N('3'):
        case TJS_N('4'):
        case TJS_N('5'):
        case TJS_N('6'):
        case TJS_N('7'):
        case TJS_N('8'):
        case TJS_N('9'):
        {
            // number
            tTJSVariant val;
            try
            {
                if (!TJSParseNumber(val, &Current))
                    return PT_ERROR;
            }
            catch (...)
            {
                return PT_ERROR;
            }
            value = (tjs_int32)(tTVInteger)val;
            return PT_NUM;
        }
    }

    if (!TJS_iswalpha(Current) && *Current != TJS_N('_'))
    {
        return PT_ERROR;
    }

    const tjs_char* st = Current;
    while (*Current)
    {
        if (TJS_iswdigit(Current) || *Current == TJS_N('_'))
        {
            Current++;
            continue;
        }
        int alphaLen = TJS_iswalpha(Current);
        if (alphaLen > 0)
        {
            // TJS_iswalpha only inspects the leading byte; do not step over
            // the terminating NUL when a multi-byte sequence is truncated
            int available = 0;
            while (available < alphaLen && Current[available])
                available++;
            if (available < alphaLen)
                break;
            Current += alphaLen;
            continue;
        }
        break;
    }

    ttstr str(st, (int)(Current - st));

    IDs.push_back(str);
    value = (tjs_int32)(IDs.size() - 1);

    return PT_SYMBOL;
}
//---------------------------------------------------------------------------
const tjs_char* tTJSPPExprParser::GetString(tjs_int idx) const
{
    return IDs[idx].c_str();
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
} // namespace TJS
