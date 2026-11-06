//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Hand written TJS2 parser.
//
// This replaces the bison generated parser (syntax/tjs.y).  The productions,
// their order and every semantic action are kept identical to the generated
// parser so the emitted intermediate code is the same; the parser is a
// recursive descent one and does not allocate a bison parse stack.
//
// Token context flags (SetNextIsBareWord / SetStartOfRegExp) are applied
// before the affected token is fetched.  Tokens are therefore fetched lazily.
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <deque>

#include "tjsParserTokens.h"
#include "tjsInterCodeGen.h"
#include "tjsScriptBlock.h"
#include "tjsError.h"
#include "tjsArray.h"
#include "tjsDictionary.h"

namespace TJS
{
//---------------------------------------------------------------------------
// implemented in tjsInterCodeGen.cpp
int _yyerror(const tjs_char* msg, void* pm, tjs_int pos);
//---------------------------------------------------------------------------

namespace
{
//---------------------------------------------------------------------------
struct tTJSToken
{
    int Id;
    tjs_int Value;
    tjs_int Pos;

    tTJSToken() : Id(0), Value(0), Pos(0) {}
};
//---------------------------------------------------------------------------
const tjs_char* TJSTokenName(int id)
{
    switch (id)
    {
        case 0:
            return TJS_N("end of file");
        case T_COMMA:
            return TJS_N("','");
        case T_EQUAL:
            return TJS_N("'='");
        case T_AMPERSANDEQUAL:
            return TJS_N("'&='");
        case T_VERTLINEEQUAL:
            return TJS_N("'|='");
        case T_CHEVRONEQUAL:
            return TJS_N("'^='");
        case T_MINUSEQUAL:
            return TJS_N("'-='");
        case T_PLUSEQUAL:
            return TJS_N("'+='");
        case T_PERCENTEQUAL:
            return TJS_N("'%='");
        case T_SLASHEQUAL:
            return TJS_N("'/='");
        case T_BACKSLASHEQUAL:
            return TJS_N("'\\\\='");
        case T_ASTERISKEQUAL:
            return TJS_N("'*='");
        case T_LOGICALOREQUAL:
            return TJS_N("'||='");
        case T_LOGICALANDEQUAL:
            return TJS_N("'&&='");
        case T_RBITSHIFTEQUAL:
            return TJS_N("'>>>='");
        case T_LARITHSHIFTEQUAL:
            return TJS_N("'<<='");
        case T_RARITHSHIFTEQUAL:
            return TJS_N("'>>='");
        case T_QUESTION:
            return TJS_N("'?'");
        case T_LOGICALOR:
            return TJS_N("'||'");
        case T_LOGICALAND:
            return TJS_N("'&&'");
        case T_VERTLINE:
            return TJS_N("'|'");
        case T_CHEVRON:
            return TJS_N("'^'");
        case T_AMPERSAND:
            return TJS_N("'&'");
        case T_NOTEQUAL:
            return TJS_N("'!='");
        case T_EQUALEQUAL:
            return TJS_N("'=='");
        case T_DISCNOTEQUAL:
            return TJS_N("'!=='");
        case T_DISCEQUAL:
            return TJS_N("'==='");
        case T_SWAP:
            return TJS_N("'<->'");
        case T_LT:
            return TJS_N("'<'");
        case T_GT:
            return TJS_N("'>'");
        case T_LTOREQUAL:
            return TJS_N("'<='");
        case T_GTOREQUAL:
            return TJS_N("'>='");
        case T_RARITHSHIFT:
            return TJS_N("'>>'");
        case T_LARITHSHIFT:
            return TJS_N("'<<'");
        case T_RBITSHIFT:
            return TJS_N("'>>>'");
        case T_PERCENT:
            return TJS_N("'%'");
        case T_SLASH:
            return TJS_N("'/'");
        case T_BACKSLASH:
            return TJS_N("'\\\\'");
        case T_ASTERISK:
            return TJS_N("'*'");
        case T_EXCRAMATION:
            return TJS_N("'!'");
        case T_TILDE:
            return TJS_N("'~'");
        case T_DECREMENT:
            return TJS_N("'--'");
        case T_INCREMENT:
            return TJS_N("'++'");
        case T_NEW:
            return TJS_N("'new'");
        case T_DELETE:
            return TJS_N("'delete'");
        case T_TYPEOF:
            return TJS_N("'typeof'");
        case T_PLUS:
            return TJS_N("'+'");
        case T_MINUS:
            return TJS_N("'-'");
        case T_SHARP:
            return TJS_N("'#'");
        case T_DOLLAR:
            return TJS_N("'$'");
        case T_ISVALID:
            return TJS_N("'isvalid'");
        case T_INVALIDATE:
            return TJS_N("'invalidate'");
        case T_INSTANCEOF:
            return TJS_N("'instanceof'");
        case T_LPARENTHESIS:
            return TJS_N("'('");
        case T_DOT:
            return TJS_N("'.'");
        case T_LBRACKET:
            return TJS_N("'['");
        case T_THIS:
            return TJS_N("'this'");
        case T_SUPER:
            return TJS_N("'super'");
        case T_GLOBAL:
            return TJS_N("'global'");
        case T_RBRACKET:
            return TJS_N("']'");
        case T_CLASS:
            return TJS_N("'class'");
        case T_RPARENTHESIS:
            return TJS_N("')'");
        case T_COLON:
            return TJS_N("':'");
        case T_SEMICOLON:
            return TJS_N("';'");
        case T_LBRACE:
            return TJS_N("'{'");
        case T_RBRACE:
            return TJS_N("'}'");
        case T_CONTINUE:
            return TJS_N("'continue'");
        case T_FUNCTION:
            return TJS_N("'function'");
        case T_DEBUGGER:
            return TJS_N("'debugger'");
        case T_DEFAULT:
            return TJS_N("'default'");
        case T_CASE:
            return TJS_N("'case'");
        case T_EXTENDS:
            return TJS_N("'extends'");
        case T_FINALLY:
            return TJS_N("'finally'");
        case T_PROPERTY:
            return TJS_N("'property'");
        case T_PRIVATE:
            return TJS_N("'private'");
        case T_PUBLIC:
            return TJS_N("'public'");
        case T_PROTECTED:
            return TJS_N("'protected'");
        case T_STATIC:
            return TJS_N("'static'");
        case T_RETURN:
            return TJS_N("'return'");
        case T_BREAK:
            return TJS_N("'break'");
        case T_EXPORT:
            return TJS_N("'export'");
        case T_IMPORT:
            return TJS_N("'import'");
        case T_SWITCH:
            return TJS_N("'switch'");
        case T_IN:
            return TJS_N("'in'");
        case T_INCONTEXTOF:
            return TJS_N("'incontextof'");
        case T_FOR:
            return TJS_N("'for'");
        case T_WHILE:
            return TJS_N("'while'");
        case T_DO:
            return TJS_N("'do'");
        case T_IF:
            return TJS_N("'if'");
        case T_VAR:
            return TJS_N("'var'");
        case T_CONST:
            return TJS_N("'const'");
        case T_ENUM:
            return TJS_N("'enum'");
        case T_GOTO:
            return TJS_N("'goto'");
        case T_THROW:
            return TJS_N("'throw'");
        case T_TRY:
            return TJS_N("'try'");
        case T_SETTER:
            return TJS_N("'setter'");
        case T_GETTER:
            return TJS_N("'getter'");
        case T_ELSE:
            return TJS_N("'else'");
        case T_CATCH:
            return TJS_N("'catch'");
        case T_OMIT:
            return TJS_N("'...'");
        case T_SYNCHRONIZED:
            return TJS_N("'synchronized'");
        case T_WITH:
            return TJS_N("'with'");
        case T_INT:
            return TJS_N("'int'");
        case T_REAL:
            return TJS_N("'real'");
        case T_STRING:
            return TJS_N("'string'");
        case T_OCTET:
            return TJS_N("'octet'");
        case T_VOID:
            return TJS_N("'void'");
        case T_CONSTVAL:
            return TJS_N("constant");
        case T_SYMBOL:
            return TJS_N("identifier");
        case T_REGEXP:
            return TJS_N("regular expression");
        default:
            return TJS_N("token");
    }
}
//---------------------------------------------------------------------------
} // namespace

//---------------------------------------------------------------------------
// tTJSParser
//---------------------------------------------------------------------------
class tTJSParser
{
public:
    explicit tTJSParser(tTJSScriptBlock* block) : Block(block), TokenCount(0)
    {
        Lexer = Block->GetLexicalAnalyzer();
        Aborted = false;
    }

    int Parse();

private:
    tTJSScriptBlock* Block;
    tTJSLexicalAnalyzer* Lexer;
    // Lookahead only ever needs five tokens (the (const)[...] forms).  A
    // fixed array avoids the per-token allocation of a deque.
    enum
    {
        TokenBufferSize = 8
    };
    tTJSToken Tokens[TokenBufferSize];
    size_t TokenCount;
    bool Aborted;

private:
    tTJSInterCodeContext* Ctx() { return Block->GetCurrentContext(); }

    void Fill(size_t count)
    {
        while (TokenCount <= count)
        {
            tTJSToken& tok = Tokens[TokenCount];
            tok.Value = 0;
            tok.Id = Lexer->GetNext(tok.Value);
            tok.Pos = Lexer->GetTokenStartPosition();
            TokenCount++;
        }
    }

    int Peek(size_t depth = 0)
    {
        Fill(depth);
        return Tokens[depth].Id;
    }

    const tTJSToken& Token(size_t depth = 0)
    {
        Fill(depth);
        return Tokens[depth];
    }

    tjs_int TokenPos(size_t depth = 0) { return Token(depth).Pos; }

    // Consumes the lookahead token without fetching the next one.  Fetching is
    // always lazy so SetNextIsBareWord / SetStartOfRegExp can be applied first.
    void Drop()
    {
        if (TokenCount)
        {
            for (size_t i = 1; i < TokenCount; i++)
                Tokens[i - 1] = Tokens[i];
            TokenCount--;
        }
    }

    void Consume()
    {
        Fill(0);
        Drop();
    }

    bool Accept(int id)
    {
        if (Peek() == id)
        {
            Drop();
            return true;
        }
        return false;
    }

    bool Error(const ttstr& msg, tjs_int pos)
    {
        // A syntax error leaves the code generator half way through a
        // construct; stop at the first one instead of trying to repair the
        // generator's nesting stack (which was never designed for recovery).
        Aborted = true;
        _yyerror(msg.c_str(), Block, pos);
        return false;
    }

    bool Unexpected(const tjs_char* expected)
    {
        ttstr msg(TJS_N("syntax error, unexpected "));
        msg += TJSTokenName(Peek());
        if (expected && *expected)
        {
            msg += TJS_N(", expecting ");
            msg += expected;
        }
        return Error(msg, TokenPos());
    }

    bool Expect(int id, const tjs_char* what)
    {
        if (Peek() == id)
        {
            Drop();
            return true;
        }
        return Unexpected(what ? what : TJSTokenName(id));
    }

    bool Take(int id, tTJSToken& out)
    {
        if (Peek() != id)
            return Unexpected(TJSTokenName(id));
        out = Token();
        Drop();
        return true;
    }

    bool Recover()
    {
        // Mirrors the "def_list error ';'" production: discard up to and
        // including the next semicolon.  A closing brace belongs to the
        // enclosing block and is left in place.
        while (true)
        {
            int id = Peek();
            if (id == 0 || id == T_RBRACE)
                return true;
            Drop();
            if (id == T_SEMICOLON)
                return true;
        }
    }

    bool CheckAbort()
    {
        if (Block->CompileErrorCount > 20)
        {
            Aborted = true;
            return true;
        }
        return false;
    }

    // statements / declarations ---------------------------------------------
    bool ParseDefList(bool stop_at_brace);
    bool ParseBlockOrStatement();
    bool ParseBlock();
    bool ParseStatement();
    bool ParseIf();
    bool ParseWhile();
    bool ParseDoWhile();
    bool ParseFor();
    bool ParseVariableDef();
    bool ParseVariableDefInner();
    bool ParseVariableType();
    bool ParseFunctionDef();
    bool ParseFunctionExprDef(tTJSExprNode** out);
    bool ParseFunctionDeclArgs();
    bool ParseOneFunctionDeclArg(bool& was_collapse);
    bool ParsePropertyDef();
    bool ParseSetter();
    bool ParseGetter();
    bool ParseClassDef();
    bool ParseClassExtender();
    bool ParseReturn();
    bool ParseSwitch();
    bool ParseWith();
    bool ParseCase();
    bool ParseTry();
    bool ParseThrow();

    // expressions -----------------------------------------------------------
    bool ParseExpr(tTJSExprNode** out);
    bool ParseExprNoComma(tTJSExprNode** out);
    bool ParseCommaExpr(tTJSExprNode** out);
    bool ParseAssignExpr(tTJSExprNode** out, bool allow_expand = false);
    bool ParseCondExpr(tTJSExprNode** out, bool allow_expand);
    bool ParseLogicalOr(tTJSExprNode** out, bool allow_expand);
    bool ParseLogicalAnd(tTJSExprNode** out, bool allow_expand);
    bool ParseInclusiveOr(tTJSExprNode** out, bool allow_expand);
    bool ParseExclusiveOr(tTJSExprNode** out, bool allow_expand);
    bool ParseAndExpr(tTJSExprNode** out, bool allow_expand);
    bool ParseIdentical(tTJSExprNode** out, bool allow_expand);
    bool ParseCompare(tTJSExprNode** out, bool allow_expand);
    bool ParseShift(tTJSExprNode** out, bool allow_expand);
    bool ParseAddSub(tTJSExprNode** out, bool allow_expand);
    bool ParseMulDiv(tTJSExprNode** out, bool allow_expand);
    bool ParseUnary(tTJSExprNode** out);
    bool ParseInContextOf(tTJSExprNode** out);
    bool ParsePriority(tTJSExprNode** out, bool* last_was_call);
    bool ParseFactor(tTJSExprNode** out);
    bool ParseNewTarget(tTJSExprNode** out);
    bool ParseCallArgList(tTJSExprNode** out);
    bool ParseCallArg(tTJSExprNode** out);
    bool ParseInlineArray(tTJSExprNode** out);
    bool ParseArrayElement(tTJSExprNode** out);
    bool ParseInlineDic(tTJSExprNode** out);
    bool ParseDicElement(tTJSExprNode** out);
    bool ParseConstInlineArray(tTJSExprNode** out);
    bool ParseConstInlineDic(tTJSExprNode** out);
    bool ParseConstArrayElement();
    bool ParseConstDicElement();

    bool IsConstArrayStart()
    {
        return Peek(0) == T_LPARENTHESIS && Peek(1) == T_CONST && Peek(2) == T_RPARENTHESIS &&
               Peek(3) == T_LBRACKET;
    }

    bool IsConstDicStart()
    {
        return Peek(0) == T_LPARENTHESIS && Peek(1) == T_CONST && Peek(2) == T_RPARENTHESIS &&
               Peek(3) == T_PERCENT && Peek(4) == T_LBRACKET;
    }

    tTJSExprNode* MakeConstNode(const tTJSVariant& val)
    {
        tTJSExprNode* n = Ctx()->MakeNP0(T_CONSTVAL);
        n->SetValue(val);
        return n;
    }
};

//---------------------------------------------------------------------------
// program / global_list / def_list
//---------------------------------------------------------------------------
bool tTJSParser::ParseDefList(bool stop_at_brace)
{
    while (true)
    {
        int id = Peek();
        if (id == 0)
            return true;
        if (stop_at_brace && id == T_RBRACE)
            return true;

        if (!ParseBlockOrStatement())
        {
            if (CheckAbort() || Aborted)
                return false;
            Recover();
            continue;
        }
    }
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseBlockOrStatement()
{
    if (Peek() == T_LBRACE)
        return ParseBlock();
    return ParseStatement();
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseBlock()
{
    if (!Expect(T_LBRACE, TJS_N("'{' to begin a block")))
        return false;

    Ctx()->EnterBlock();
    bool ok = ParseDefList(true);
    if (ok)
    {
        ok = Expect(T_RBRACE, TJS_N("'}' to close the block"));
        if (ok)
            Ctx()->ExitBlock();
    }
    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseStatement()
{
    switch (Peek())
    {
        case T_SEMICOLON:
            Drop();
            return true;

        case T_LBRACE:
            return ParseBlock();

        case T_BREAK:
            Consume();
            if (!Expect(T_SEMICOLON, TJS_N("';' after 'break'")))
                return false;
            Ctx()->DoBreak();
            return true;

        case T_CONTINUE:
            Consume();
            if (!Expect(T_SEMICOLON, TJS_N("';' after 'continue'")))
                return false;
            Ctx()->DoContinue();
            return true;

        case T_DEBUGGER:
            Consume();
            if (!Expect(T_SEMICOLON, TJS_N("';' after 'debugger'")))
                return false;
            Ctx()->DoDebugger();
            return true;

        case T_IF:
            return ParseIf();
        case T_WHILE:
            return ParseWhile();
        case T_DO:
            return ParseDoWhile();
        case T_FOR:
            return ParseFor();
        case T_VAR:
        case T_CONST:
            return ParseVariableDef();
        case T_PROPERTY:
            return ParsePropertyDef();
        case T_CLASS:
            return ParseClassDef();
        case T_RETURN:
            return ParseReturn();
        case T_SWITCH:
            return ParseSwitch();
        case T_WITH:
            return ParseWith();
        case T_CASE:
        case T_DEFAULT:
            return ParseCase();
        case T_TRY:
            return ParseTry();
        case T_THROW:
            return ParseThrow();

        case T_FUNCTION:
            if (Peek(1) == T_SYMBOL)
                return ParseFunctionDef();
            break; // anonymous function expression statement

        default:
            break;
    }

    tTJSExprNode* expr = NULL;
    if (!ParseExpr(&expr))
        return false;
    if (!Expect(T_SEMICOLON, TJS_N("';' after the expression")))
        return false;
    Ctx()->CreateExprCode(expr);
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseIf()
{
    Consume(); // 'if'
    if (!Expect(T_LPARENTHESIS, TJS_N("'(' after 'if'")))
        return false;

    Ctx()->EnterIfCode();

    tTJSExprNode* cond = NULL;
    bool ok = ParseExpr(&cond);
    if (ok)
        Ctx()->CreateIfExprCode(cond);
    if (ok)
        ok = Expect(T_RPARENTHESIS, TJS_N("')' after the if condition"));
    if (ok)
        ok = ParseBlockOrStatement();

    if (ok)
    {
        Ctx()->ExitIfCode();
        if (Accept(T_ELSE))
        {
            Ctx()->EnterElseCode();
            ok = ParseBlockOrStatement();
            if (ok)
                Ctx()->ExitElseCode();
        }
    }

    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseWhile()
{
    Consume(); // 'while'
    Ctx()->EnterWhileCode(false);
    if (!Expect(T_LPARENTHESIS, TJS_N("'(' after 'while'")))
        return false;

    tTJSExprNode* cond = NULL;
    bool ok = ParseExpr(&cond);
    if (ok)
        ok = Expect(T_RPARENTHESIS, TJS_N("')' after the while condition"));
    if (ok)
    {
        Ctx()->CreateWhileExprCode(cond, false);
        ok = ParseBlockOrStatement();
    }

    if (ok)
        Ctx()->ExitWhileCode(false);
    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseDoWhile()
{
    Consume(); // 'do'
    Ctx()->EnterWhileCode(true);
    bool ok = ParseBlockOrStatement();
    if (ok)
        ok = Expect(T_WHILE, TJS_N("'while' after the do body"));
    if (ok)
        ok = Expect(T_LPARENTHESIS, TJS_N("'(' after 'while'"));
    tTJSExprNode* cond = NULL;
    if (ok)
        ok = ParseExpr(&cond);
    if (ok)
        ok = Expect(T_RPARENTHESIS, TJS_N("')' after the while condition"));
    if (ok)
        Ctx()->CreateWhileExprCode(cond, true);
    if (ok)
        ok = Expect(T_SEMICOLON, TJS_N("';' after 'while (...)'"));
    if (ok)
        Ctx()->ExitWhileCode(true);
    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseFor()
{
    Consume(); // 'for'
    if (!Expect(T_LPARENTHESIS, TJS_N("'(' after 'for'")))
        return false;

    bool ok = true;
    int first = Peek();
    if (first == T_VAR || first == T_CONST)
    {
        Ctx()->EnterForCode();
        ok = ParseVariableDefInner();
    }
    else if (first == T_SEMICOLON)
    {
        Ctx()->EnterForCode();
    }
    else
    {
        tTJSExprNode* init = NULL;
        Ctx()->EnterForCode();
        ok = ParseExpr(&init);
        if (ok)
            Ctx()->CreateExprCode(init);
    }

    if (ok)
        ok = Expect(T_SEMICOLON, TJS_N("';' after the for initializer"));

    if (ok)
    {
        if (Peek() == T_SEMICOLON)
            Ctx()->CreateForExprCode(NULL);
        else
        {
            tTJSExprNode* cond = NULL;
            ok = ParseExpr(&cond);
            if (ok)
                Ctx()->CreateForExprCode(cond);
        }
    }

    if (ok)
        ok = Expect(T_SEMICOLON, TJS_N("';' after the for condition"));

    if (ok)
    {
        if (Peek() == T_RPARENTHESIS)
            Ctx()->SetForThirdExprCode(NULL);
        else
        {
            tTJSExprNode* step = NULL;
            ok = ParseExpr(&step);
            if (ok)
                Ctx()->SetForThirdExprCode(step);
        }
    }

    if (ok)
        ok = Expect(T_RPARENTHESIS, TJS_N("')' after the for clauses"));
    if (ok)
        ok = ParseBlockOrStatement();

    if (ok)
        Ctx()->ExitForCode();
    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseVariableType()
{
    if (!Accept(T_COLON))
        return true;

    switch (Peek())
    {
        case T_SYMBOL:
        case T_VOID:
        case T_INT:
        case T_REAL:
        case T_STRING:
        case T_OCTET:
            Drop();
            return true;
        default:
            return Unexpected(TJS_N("a type name after ':'"));
    }
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseVariableDefInner()
{
    // current token is 'var' or 'const'
    Consume();

    while (true)
    {
        tTJSToken name;
        if (!Take(T_SYMBOL, name))
            return false;

        if (!ParseVariableType())
            return false;

        if (Accept(T_EQUAL))
        {
            tTJSExprNode* init = NULL;
            if (!ParseExprNoComma(&init))
                return false;
            Ctx()->InitLocalVariable(Lexer->GetString(name.Value), init);
        }
        else
        {
            Ctx()->AddLocalVariable(Lexer->GetString(name.Value));
        }

        if (!Accept(T_COMMA))
            return true;
    }
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseVariableDef()
{
    if (!ParseVariableDefInner())
        return false;
    return Expect(T_SEMICOLON, TJS_N("';' after the variable declaration"));
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseOneFunctionDeclArg(bool& was_collapse)
{
    tTJSToken name;
    if (!Take(T_SYMBOL, name))
        return false;
    if (!ParseVariableType())
        return false;

    if (Accept(T_ASTERISK))
    {
        Ctx()->AddFunctionDeclArgCollapse(Lexer->GetString(name.Value));
        was_collapse = true;
        return true;
    }

    was_collapse = false;
    if (Accept(T_EQUAL))
    {
        tTJSExprNode* init = NULL;
        if (!ParseExprNoComma(&init))
            return false;
        Ctx()->AddFunctionDeclArg(Lexer->GetString(name.Value), init);
    }
    else
    {
        Ctx()->AddFunctionDeclArg(Lexer->GetString(name.Value), NULL);
    }
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseFunctionDeclArgs()
{
    if (!Accept(T_LPARENTHESIS))
        return true; // no argument list at all

    if (Accept(T_RPARENTHESIS))
        return true; // ()

    if (Accept(T_ASTERISK))
    {
        Ctx()->AddFunctionDeclArgCollapse(NULL);
        return Expect(T_RPARENTHESIS, TJS_N("')' after the argument list"));
    }

    bool was_collapse = false;
    if (!ParseOneFunctionDeclArg(was_collapse))
        return false;
    if (was_collapse)
        return Expect(T_RPARENTHESIS, TJS_N("')' after the argument list"));

    while (Accept(T_COMMA))
    {
        if (Accept(T_ASTERISK))
        {
            Ctx()->AddFunctionDeclArgCollapse(NULL);
            return Expect(T_RPARENTHESIS, TJS_N("')' after the argument list"));
        }
        if (!ParseOneFunctionDeclArg(was_collapse))
            return false;
        if (was_collapse)
            break;
    }

    return Expect(T_RPARENTHESIS, TJS_N("')' after the argument list"));
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseFunctionDef()
{
    Consume(); // 'function'
    tTJSToken name;
    if (!Take(T_SYMBOL, name))
        return false;

    Block->PushContextStack(Lexer->GetString(name.Value), ctFunction);
    Ctx()->EnterBlock();

    bool ok = ParseFunctionDeclArgs();
    if (ok)
        ok = ParseVariableType();
    if (ok)
    {
        ok = ParseBlock();
        if (ok)
        {
            Ctx()->ExitBlock();
            Block->PopContextStack();
        }
    }
    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseFunctionExprDef(tTJSExprNode** out)
{
    Consume(); // 'function'

    Block->PushContextStack(TJS_N("(anonymous)"), ctExprFunction);
    Ctx()->EnterBlock();

    bool ok = ParseFunctionDeclArgs();
    if (ok)
        ok = ParseVariableType();
    if (ok)
        ok = ParseBlock();
    if (!ok)
        return false;

    Ctx()->ExitBlock();

    tTJSVariant func(Ctx());
    Block->PopContextStack();

    tTJSExprNode* node = Ctx()->MakeNP0(T_CONSTVAL);
    node->SetValue(func);
    *out = node;
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParsePropertyDef()
{
    Consume(); // 'property'
    tTJSToken name;
    if (!Take(T_SYMBOL, name))
        return false;
    if (!Expect(T_LBRACE, TJS_N("'{' after the property name")))
        return false;

    Block->PushContextStack(Lexer->GetString(name.Value), ctProperty);

    bool ok = true;
    bool first_setter = false;
    if (Peek() == T_SETTER)
    {
        first_setter = true;
        ok = ParseSetter();
    }
    else if (Peek() == T_GETTER)
    {
        ok = ParseGetter();
    }
    else
    {
        ok = Unexpected(TJS_N("'setter' or 'getter'"));
    }

    if (ok && (Peek() == T_SETTER || Peek() == T_GETTER))
    {
        if (first_setter)
            ok = ParseGetter();
        else
            ok = ParseSetter();
    }

    if (ok)
    {
        ok = Expect(T_RBRACE, TJS_N("'}' to close the property definition"));
        if (ok)
            Block->PopContextStack();
    }
    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseSetter()
{
    Consume(); // 'setter'
    if (!Expect(T_LPARENTHESIS, TJS_N("'(' after 'setter'")))
        return false;
    tTJSToken arg;
    if (!Take(T_SYMBOL, arg))
        return false;
    if (!ParseVariableType())
        return false;
    if (!Expect(T_RPARENTHESIS, TJS_N("')' after the setter argument")))
        return false;

    Block->PushContextStack(TJS_N("(setter)"), ctPropertySetter);
    Ctx()->EnterBlock();
    Ctx()->SetPropertyDeclArg(Lexer->GetString(arg.Value));

    bool ok = ParseBlock();

    if (ok)
    {
        Ctx()->ExitBlock();
        Block->PopContextStack();
    }
    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseGetter()
{
    Consume(); // 'getter'
    if (Accept(T_LPARENTHESIS))
    {
        if (!Expect(T_RPARENTHESIS, TJS_N("')' after 'getter('")))
            return false;
    }
    if (!ParseVariableType())
        return false;

    Block->PushContextStack(TJS_N("(getter)"), ctPropertyGetter);
    Ctx()->EnterBlock();

    bool ok = ParseBlock();

    if (ok)
    {
        Ctx()->ExitBlock();
        Block->PopContextStack();
    }
    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseClassExtender()
{
    if (!Accept(T_EXTENDS))
        return true;

    tTJSExprNode* base = NULL;
    if (!ParseExprNoComma(&base))
        return false;

    if (!Accept(T_COMMA))
    {
        Ctx()->CreateExtendsExprCode(base, true);
        return true;
    }

    Ctx()->CreateExtendsExprCode(base, false);
    while (true)
    {
        tTJSExprNode* next = NULL;
        if (!ParseExprNoComma(&next))
            return false;
        Ctx()->CreateExtendsExprCode(next, false);
        if (!Accept(T_COMMA))
            return true;
    }
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseClassDef()
{
    Consume(); // 'class'
    tTJSToken name;
    if (!Take(T_SYMBOL, name))
        return false;

    Block->PushContextStack(Lexer->GetString(name.Value), ctClass);

    bool ok = ParseClassExtender();
    if (ok)
    {
        ok = ParseBlock();
        if (ok)
            Block->PopContextStack();
    }
    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseReturn()
{
    Consume(); // 'return'
    if (Accept(T_SEMICOLON))
    {
        Ctx()->ReturnFromFunc(NULL);
        return true;
    }

    tTJSExprNode* value = NULL;
    if (!ParseExpr(&value))
        return false;
    if (!Expect(T_SEMICOLON, TJS_N("';' after the return value")))
        return false;
    Ctx()->ReturnFromFunc(value);
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseSwitch()
{
    Consume(); // 'switch'
    if (!Expect(T_LPARENTHESIS, TJS_N("'(' after 'switch'")))
        return false;

    tTJSExprNode* value = NULL;
    bool ok = ParseExpr(&value);
    if (ok)
        ok = Expect(T_RPARENTHESIS, TJS_N("')' after the switch value"));
    if (ok)
        Ctx()->EnterSwitchCode(value);
    if (ok)
        ok = ParseBlock();
    if (ok)
        Ctx()->ExitSwitchCode();
    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseWith()
{
    Consume(); // 'with'
    if (!Expect(T_LPARENTHESIS, TJS_N("'(' after 'with'")))
        return false;

    tTJSExprNode* value = NULL;
    bool ok = ParseExpr(&value);
    if (ok)
        ok = Expect(T_RPARENTHESIS, TJS_N("')' after the with expression"));
    if (ok)
        Ctx()->EnterWithCode(value);
    if (ok)
        ok = ParseBlockOrStatement();
    if (ok)
        Ctx()->ExitWithCode();
    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseCase()
{
    if (Accept(T_CASE))
    {
        tTJSExprNode* value = NULL;
        if (!ParseExpr(&value))
            return false;
        if (!Expect(T_COLON, TJS_N("':' after the case value")))
            return false;
        Ctx()->ProcessCaseCode(value);
        return true;
    }

    Consume(); // 'default'
    if (!Expect(T_COLON, TJS_N("':' after 'default'")))
        return false;
    Ctx()->ProcessCaseCode(NULL);
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseTry()
{
    Consume(); // 'try'
    Ctx()->EnterTryCode();

    bool ok = ParseBlockOrStatement();

    if (ok)
    {
        if (!Expect(T_CATCH, TJS_N("'catch' after the try block")))
            ok = false;
    }

    if (ok)
    {
        if (Accept(T_LPARENTHESIS))
        {
            if (Accept(T_RPARENTHESIS))
                Ctx()->EnterCatchCode(NULL);
            else
            {
                tTJSToken name;
                if (!Take(T_SYMBOL, name))
                    ok = false;
                if (ok && !Expect(T_RPARENTHESIS, TJS_N("')' after the catch variable")))
                    ok = false;
                if (ok)
                    Ctx()->EnterCatchCode(Lexer->GetString(name.Value));
            }
        }
        else
        {
            Ctx()->EnterCatchCode(NULL);
        }
    }

    if (ok)
        ok = ParseBlockOrStatement();

    if (ok)
        Ctx()->ExitTryCode();
    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseThrow()
{
    Consume(); // 'throw'
    tTJSExprNode* value = NULL;
    if (!ParseExpr(&value))
        return false;
    if (!Expect(T_SEMICOLON, TJS_N("';' after the thrown value")))
        return false;
    Ctx()->ProcessThrowCode(value);
    return true;
}

//---------------------------------------------------------------------------
// expressions
//---------------------------------------------------------------------------
bool tTJSParser::ParseExpr(tTJSExprNode** out)
{
    tTJSExprNode* expr = NULL;
    if (!ParseCommaExpr(&expr))
        return false;

    if (Accept(T_IF))
    {
        tTJSExprNode* cond = NULL;
        if (!ParseExpr(&cond))
            return false;
        expr = Ctx()->MakeNP2(T_IF, expr, cond);
    }

    *out = expr;
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseExprNoComma(tTJSExprNode** out)
{
    return ParseAssignExpr(out, false);
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseCommaExpr(tTJSExprNode** out)
{
    tTJSExprNode* expr = NULL;
    if (!ParseAssignExpr(&expr, false))
        return false;

    while (Accept(T_COMMA))
    {
        tTJSExprNode* right = NULL;
        if (!ParseAssignExpr(&right, false))
            return false;
        expr = Ctx()->MakeNP2(T_COMMA, expr, right);
    }

    *out = expr;
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseAssignExpr(tTJSExprNode** out, bool allow_expand)
{
    tTJSExprNode* expr = NULL;
    if (!ParseCondExpr(&expr, allow_expand))
        return false;

    int op;
    switch (Peek())
    {
        case T_SWAP:
            op = T_SWAP;
            break;
        case T_EQUAL:
            op = T_EQUAL;
            break;
        case T_AMPERSANDEQUAL:
            op = T_AMPERSANDEQUAL;
            break;
        case T_VERTLINEEQUAL:
            op = T_VERTLINEEQUAL;
            break;
        case T_CHEVRONEQUAL:
            op = T_CHEVRONEQUAL;
            break;
        case T_MINUSEQUAL:
            op = T_MINUSEQUAL;
            break;
        case T_PLUSEQUAL:
            op = T_PLUSEQUAL;
            break;
        case T_PERCENTEQUAL:
            op = T_PERCENTEQUAL;
            break;
        case T_SLASHEQUAL:
            op = T_SLASHEQUAL;
            break;
        case T_BACKSLASHEQUAL:
            op = T_BACKSLASHEQUAL;
            break;
        case T_ASTERISKEQUAL:
            op = T_ASTERISKEQUAL;
            break;
        case T_LOGICALOREQUAL:
            op = T_LOGICALOREQUAL;
            break;
        case T_LOGICALANDEQUAL:
            op = T_LOGICALANDEQUAL;
            break;
        case T_RARITHSHIFTEQUAL:
            op = T_RARITHSHIFTEQUAL;
            break;
        case T_LARITHSHIFTEQUAL:
            op = T_LARITHSHIFTEQUAL;
            break;
        case T_RBITSHIFTEQUAL:
            op = T_RBITSHIFTEQUAL;
            break;
        default:
            *out = expr;
            return true;
    }

    // Assignment is right associative and its right hand side is a complete
    // assign_expr; call-argument expansion is not applicable there.
    Drop();
    tTJSExprNode* right = NULL;
    if (!ParseAssignExpr(&right, false))
        return false;
    *out = Ctx()->MakeNP2(op, expr, right);
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseCondExpr(tTJSExprNode** out, bool allow_expand)
{
    tTJSExprNode* expr = NULL;
    if (!ParseLogicalOr(&expr, allow_expand))
        return false;

    if (Accept(T_QUESTION))
    {
        tTJSExprNode* then_expr = NULL;
        if (!ParseCondExpr(&then_expr, false))
            return false;
        if (!Expect(T_COLON, TJS_N("':' in the conditional expression")))
            return false;
        tTJSExprNode* else_expr = NULL;
        if (!ParseCondExpr(&else_expr, false))
            return false;
        expr = Ctx()->MakeNP3(T_QUESTION, expr, then_expr, else_expr);
    }

    *out = expr;
    return true;
}

//---------------------------------------------------------------------------
#define TJS_BINARY_LEVEL(name, next, token) \
    bool tTJSParser::name(tTJSExprNode** out, bool allow_expand) \
    { \
        tTJSExprNode* expr = NULL; \
        if (!next(&expr, allow_expand)) \
            return false; \
        while (Peek() == token) \
        { \
            Drop(); \
            tTJSExprNode* right = NULL; \
            if (!next(&right, false)) \
                return false; \
            expr = Ctx()->MakeNP2(token, expr, right); \
        } \
        *out = expr; \
        return true; \
    }

TJS_BINARY_LEVEL(ParseLogicalOr, ParseLogicalAnd, T_LOGICALOR)
TJS_BINARY_LEVEL(ParseLogicalAnd, ParseInclusiveOr, T_LOGICALAND)
TJS_BINARY_LEVEL(ParseInclusiveOr, ParseExclusiveOr, T_VERTLINE)
TJS_BINARY_LEVEL(ParseExclusiveOr, ParseAndExpr, T_CHEVRON)
TJS_BINARY_LEVEL(ParseAndExpr, ParseIdentical, T_AMPERSAND)
#undef TJS_BINARY_LEVEL

//---------------------------------------------------------------------------
bool tTJSParser::ParseIdentical(tTJSExprNode** out, bool allow_expand)
{
    tTJSExprNode* expr = NULL;
    if (!ParseCompare(&expr, allow_expand))
        return false;

    while (true)
    {
        int op = Peek();
        if (op != T_NOTEQUAL && op != T_EQUALEQUAL && op != T_DISCNOTEQUAL && op != T_DISCEQUAL)
            break;
        Drop();
        tTJSExprNode* right = NULL;
        if (!ParseCompare(&right, false))
            return false;
        expr = Ctx()->MakeNP2(op, expr, right);
    }

    *out = expr;
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseCompare(tTJSExprNode** out, bool allow_expand)
{
    tTJSExprNode* expr = NULL;
    if (!ParseShift(&expr, allow_expand))
        return false;

    while (true)
    {
        int op = Peek();
        if (op != T_LT && op != T_GT && op != T_LTOREQUAL && op != T_GTOREQUAL)
            break;
        Drop();
        tTJSExprNode* right = NULL;
        if (!ParseShift(&right, false))
            return false;
        expr = Ctx()->MakeNP2(op, expr, right);
    }

    *out = expr;
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseShift(tTJSExprNode** out, bool allow_expand)
{
    tTJSExprNode* expr = NULL;
    if (!ParseAddSub(&expr, allow_expand))
        return false;

    while (true)
    {
        int op = Peek();
        if (op != T_RARITHSHIFT && op != T_LARITHSHIFT && op != T_RBITSHIFT)
            break;
        Drop();
        tTJSExprNode* right = NULL;
        if (!ParseAddSub(&right, false))
            return false;
        expr = Ctx()->MakeNP2(op, expr, right);
    }

    *out = expr;
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseAddSub(tTJSExprNode** out, bool allow_expand)
{
    tTJSExprNode* expr = NULL;
    if (!ParseMulDiv(&expr, allow_expand))
        return false;

    while (Peek() == T_PLUS || Peek() == T_MINUS)
    {
        int op = Peek();
        Drop();
        tTJSExprNode* right = NULL;
        if (!ParseMulDiv(&right, false))
            return false;
        expr = Ctx()->MakeNP2(op, expr, right);
    }

    *out = expr;
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseMulDiv(tTJSExprNode** out, bool allow_expand)
{
    tTJSExprNode* expr = NULL;
    if (!ParseUnary(&expr))
        return false;

    while (true)
    {
        int op = Peek();
        if (op != T_PERCENT && op != T_SLASH && op != T_BACKSLASH && op != T_ASTERISK)
            break;

        if (op == T_ASTERISK && allow_expand)
        {
            // call_arg: mul_div_expr_and_asterisk -- "expr *" as the whole
            // argument expands the expression into the argument list.
            int next = Peek(1);
            if (next == T_COMMA || next == T_RPARENTHESIS)
            {
                Drop();
                expr = Ctx()->MakeNP1(T_EXPANDARG, expr);
                break;
            }
        }

        Drop();
        tTJSExprNode* right = NULL;
        if (!ParseUnary(&right))
            return false;
        expr = Ctx()->MakeNP2(op, expr, right);
    }

    *out = expr;
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseUnary(tTJSExprNode** out)
{
    int id = Peek();

    // (int) expr, (real) expr, (string) expr
    if (id == T_LPARENTHESIS)
    {
        int type = Peek(1);
        if ((type == T_INT || type == T_REAL || type == T_STRING) && Peek(2) == T_RPARENTHESIS)
        {
            Drop();
            Drop();
            Drop();
            tTJSExprNode* operand = NULL;
            if (!ParseUnary(&operand))
                return false;
            int op = type == T_INT ? T_INT : (type == T_REAL ? T_REAL : T_STRING);
            *out = Ctx()->MakeNP1(op, operand);
            return true;
        }
    }

    switch (id)
    {
        case T_EXCRAMATION:
        case T_TILDE:
        case T_DECREMENT:
        case T_INCREMENT:
        case T_INVALIDATE:
        case T_ISVALID:
        case T_DELETE:
        case T_TYPEOF:
        case T_SHARP:
        case T_DOLLAR:
        case T_PLUS:
        case T_MINUS:
        case T_AMPERSAND:
        case T_ASTERISK:
        {
            Drop();
            tTJSExprNode* operand = NULL;
            if (!ParseUnary(&operand))
                return false;
            int op = id;
            switch (id)
            {
                case T_PLUS:
                    op = T_UPLUS;
                    break;
                case T_MINUS:
                    op = T_UMINUS;
                    break;
                case T_AMPERSAND:
                    op = T_IGNOREPROP;
                    break;
                case T_ASTERISK:
                    op = T_PROPACCESS;
                    break;
                default:
                    break;
            }
            *out = Ctx()->MakeNP1(op, operand);
            return true;
        }

        case T_NEW:
        {
            Drop();
            tTJSExprNode* operand = NULL;
            if (!ParseNewTarget(&operand))
                return false;
            operand->SetOpecode(T_NEW);
            *out = operand;
            return true;
        }

        case T_INT:
        case T_REAL:
        case T_STRING:
        {
            Drop();
            tTJSExprNode* operand = NULL;
            if (!ParseUnary(&operand))
                return false;
            *out = Ctx()->MakeNP1(id, operand);
            return true;
        }

        default:
            break;
    }

    tTJSExprNode* expr = NULL;
    if (!ParseInContextOf(&expr))
        return false;

    if (Accept(T_ISVALID))
    {
        *out = Ctx()->MakeNP1(T_ISVALID, expr);
        return true;
    }

    if (Accept(T_INSTANCEOF))
    {
        tTJSExprNode* right = NULL;
        if (!ParseUnary(&right))
            return false;
        *out = Ctx()->MakeNP2(T_INSTANCEOF, expr, right);
        return true;
    }

    *out = expr;
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseInContextOf(tTJSExprNode** out)
{
    tTJSExprNode* expr = NULL;
    bool dummy = false;
    if (!ParsePriority(&expr, &dummy))
        return false;

    if (Accept(T_INCONTEXTOF))
    {
        tTJSExprNode* right = NULL;
        if (!ParseInContextOf(&right))
            return false;
        expr = Ctx()->MakeNP2(T_INCONTEXTOF, expr, right);
    }

    *out = expr;
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseNewTarget(tTJSExprNode** out)
{
    // func_call_expr: priority_expr "(" call_arg_list ")"
    tTJSExprNode* expr = NULL;
    bool last_was_call = false;
    if (!ParsePriority(&expr, &last_was_call))
        return false;

    if (!last_was_call)
        return Error(TJS_N("syntax error, 'new' must be followed by a constructor call"), TokenPos());

    *out = expr;
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParsePriority(tTJSExprNode** out, bool* last_was_call)
{
    tTJSExprNode* expr = NULL;

    if (IsConstArrayStart())
    {
        if (!ParseConstInlineArray(&expr))
            return false;
    }
    else if (IsConstDicStart())
    {
        if (!ParseConstInlineDic(&expr))
            return false;
    }
    else if (Peek() == T_LPARENTHESIS)
    {
        Drop();
        if (!ParseExpr(&expr))
            return false;
        if (!Expect(T_RPARENTHESIS, TJS_N("')' after the parenthesized expression")))
            return false;
    }
    else if (Peek() == T_DOT)
    {
        Drop();
        Lexer->SetNextIsBareWord();
        tTJSToken name;
        if (!Take(T_SYMBOL, name))
            return false;
        tTJSExprNode* name_node = MakeConstNode(Lexer->GetValue(name.Value));
        expr = Ctx()->MakeNP1(T_WITHDOT, name_node);
    }
    else
    {
        if (!ParseFactor(&expr))
            return false;
    }

    bool was_call = false;
    while (true)
    {
        int id = Peek();
        if (id == T_LPARENTHESIS)
        {
            Drop();
            tTJSExprNode* args = NULL;
            if (!ParseCallArgList(&args))
                return false;
            if (!Expect(T_RPARENTHESIS, TJS_N("')' after the argument list")))
                return false;
            expr = Ctx()->MakeNP2(T_LPARENTHESIS, expr, args);
            was_call = true;
        }
        else if (id == T_LBRACKET)
        {
            Drop();
            tTJSExprNode* index = NULL;
            if (!ParseExpr(&index))
                return false;
            if (!Expect(T_RBRACKET, TJS_N("']' after the index expression")))
                return false;
            expr = Ctx()->MakeNP2(T_LBRACKET, expr, index);
            was_call = false;
        }
        else if (id == T_DOT)
        {
            Drop();
            Lexer->SetNextIsBareWord();
            tTJSToken name;
            if (!Take(T_SYMBOL, name))
                return false;
            tTJSExprNode* name_node = MakeConstNode(Lexer->GetValue(name.Value));
            expr = Ctx()->MakeNP2(T_DOT, expr, name_node);
            was_call = false;
        }
        else if (id == T_INCREMENT)
        {
            Drop();
            expr = Ctx()->MakeNP1(T_POSTINCREMENT, expr);
            was_call = false;
        }
        else if (id == T_DECREMENT)
        {
            Drop();
            expr = Ctx()->MakeNP1(T_POSTDECREMENT, expr);
            was_call = false;
        }
        else if (id == T_EXCRAMATION)
        {
            Drop();
            expr = Ctx()->MakeNP1(T_EVAL, expr);
            was_call = false;
        }
        else
        {
            break;
        }
    }

    *out = expr;
    if (last_was_call)
        *last_was_call = was_call;
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseFactor(tTJSExprNode** out)
{
    switch (Peek())
    {
        case T_CONSTVAL:
        {
            tjs_int value = Token().Value;
            Drop();
            *out = MakeConstNode(Lexer->GetValue(value));
            return true;
        }

        case T_SYMBOL:
        {
            tjs_int value = Token().Value;
            Drop();
            tTJSExprNode* node = Ctx()->MakeNP0(T_SYMBOL);
            node->SetValue(tTJSVariant(Lexer->GetString(value)));
            *out = node;
            return true;
        }

        case T_THIS:
            Drop();
            *out = Ctx()->MakeNP0(T_THIS);
            return true;

        case T_SUPER:
            Drop();
            *out = Ctx()->MakeNP0(T_SUPER);
            return true;

        case T_GLOBAL:
            Drop();
            *out = Ctx()->MakeNP0(T_GLOBAL);
            return true;

        case T_VOID:
            Drop();
            *out = Ctx()->MakeNP0(T_VOID);
            return true;

        case T_FUNCTION:
            return ParseFunctionExprDef(out);

        case T_LBRACKET:
            return ParseInlineArray(out);

        case T_PERCENT:
            if (Peek(1) == T_LBRACKET)
                return ParseInlineDic(out);
            return Unexpected(TJS_N("'[' after '%'"));

        case T_SLASH:
        case T_SLASHEQUAL:
        {
            // $@23 / $@24: the lexer must re-read this '/' as the beginning of
            // a regular expression.  Do not fetch the next token before this.
            Drop();
            Lexer->SetStartOfRegExp();
            tTJSToken re;
            if (!Take(T_REGEXP, re))
                return false;
            *out = MakeConstNode(Lexer->GetValue(re.Value));
            (*out)->SetOpecode(T_REGEXP);
            return true;
        }

        default:
            return Unexpected(TJS_N("an expression"));
    }
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseCallArgList(tTJSExprNode** out)
{
    if (Peek() == T_OMIT)
    {
        Drop();
        *out = Ctx()->MakeNP0(T_OMIT);
        return true;
    }

    tTJSExprNode* arg = NULL;
    if (!ParseCallArg(&arg))
        return false;

    tTJSExprNode* list = Ctx()->MakeNP1(T_ARG, arg);
    while (Accept(T_COMMA))
    {
        if (!ParseCallArg(&arg))
            return false;
        list = Ctx()->MakeNP2(T_ARG, arg, list);
    }

    *out = list;
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseCallArg(tTJSExprNode** out)
{
    int id = Peek();
    if (id == T_COMMA || id == T_RPARENTHESIS)
    {
        *out = NULL;
        return true;
    }

    if (id == T_ASTERISK)
    {
        int next = Peek(1);
        if (next == T_COMMA || next == T_RPARENTHESIS)
        {
            Drop();
            *out = Ctx()->MakeNP1(T_EXPANDARG, NULL);
            return true;
        }
    }

    tTJSExprNode* expr = NULL;
    if (!ParseAssignExpr(&expr, true))
        return false;

    *out = expr;
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseArrayElement(tTJSExprNode** out)
{
    if (Peek() == T_RBRACKET || Peek() == T_COMMA)
    {
        *out = Ctx()->MakeNP1(T_ARRAYARG, NULL);
        return true;
    }

    tTJSExprNode* expr = NULL;
    if (!ParseExprNoComma(&expr))
        return false;
    *out = Ctx()->MakeNP1(T_ARRAYARG, expr);
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseInlineArray(tTJSExprNode** out)
{
    Drop(); // '['

    tTJSExprNode* array = Ctx()->MakeNP0(T_INLINEARRAY);
    Ctx()->PushCurrentNode(array);

    bool ok = true;
    if (Peek() == T_RBRACKET)
    {
        // array_elm_list requires one element, and array_elm may be empty.
        tTJSExprNode* element = NULL;
        ok = ParseArrayElement(&element);
        if (ok)
            Ctx()->GetCurrentNode()->Add(element);
    }
    else
    {
        while (true)
        {
            tTJSExprNode* element = NULL;
            if (!ParseArrayElement(&element))
            {
                ok = false;
                break;
            }
            Ctx()->GetCurrentNode()->Add(element);
            if (!Accept(T_COMMA))
                break;
        }
    }

    if (ok)
        ok = Expect(T_RBRACKET, TJS_N("']' after the array elements"));

    if (ok)
    {
        *out = Ctx()->GetCurrentNode();
        Ctx()->PopCurrentNode();
    }

    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseDicElement(tTJSExprNode** out)
{
    tTJSExprNode* key = NULL;
    if (Peek() == T_SYMBOL && Peek(1) == T_COLON)
    {
        tjs_int value = Token().Value;
        Drop();
        Drop(); // ':'
        key = MakeConstNode(tTJSVariant(Lexer->GetString(value)));
    }
    else
    {
        if (!ParseExprNoComma(&key))
            return false;
        if (!Expect(T_COMMA, TJS_N("',' between the dictionary key and value")))
            return false;
    }

    tTJSExprNode* value = NULL;
    if (!ParseExprNoComma(&value))
        return false;

    *out = Ctx()->MakeNP2(T_DICELM, key, value);
    return true;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseInlineDic(tTJSExprNode** out)
{
    Drop(); // '%'
    if (!Expect(T_LBRACKET, TJS_N("'[' after '%'")))
        return false;

    tTJSExprNode* dict = Ctx()->MakeNP0(T_INLINEDIC);
    Ctx()->PushCurrentNode(dict);

    bool ok = true;
    if (Peek() == T_RBRACKET)
    {
        // empty dictionary
    }
    else if (Peek() == T_COMMA && Peek(1) == T_RBRACKET)
    {
        Drop(); // trailing dummy comma
    }
    else
    {
        while (true)
        {
            tTJSExprNode* element = NULL;
            if (!ParseDicElement(&element))
            {
                ok = false;
                break;
            }
            Ctx()->GetCurrentNode()->Add(element);

            if (!Accept(T_COMMA))
                break;
            if (Peek() == T_RBRACKET)
                break; // dic_dummy_elm_opt
        }
    }

    if (ok)
        ok = Expect(T_RBRACKET, TJS_N("']' after the dictionary elements"));

    if (ok)
    {
        *out = Ctx()->GetCurrentNode();
        Ctx()->PopCurrentNode();
    }

    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseConstArrayElement()
{
    switch (Peek())
    {
        case T_MINUS:
        case T_PLUS:
        {
            int sign = Peek();
            Drop();
            tTJSToken value;
            if (!Take(T_CONSTVAL, value))
                return false;
            tTJSVariant v(Lexer->GetValue(value.Value));
            Ctx()->GetCurrentNode()->AddArrayElement(sign == T_MINUS ? -v : +v);
            return true;
        }

        case T_CONSTVAL:
        {
            tjs_int value = Token().Value;
            Drop();
            Ctx()->GetCurrentNode()->AddArrayElement(Lexer->GetValue(value));
            return true;
        }

        case T_VOID:
            Drop();
            Ctx()->GetCurrentNode()->AddArrayElement(tTJSVariant());
            return true;

        case T_LPARENTHESIS:
            if (IsConstArrayStart())
            {
                tTJSExprNode* node = NULL;
                if (!ParseConstInlineArray(&node))
                    return false;
                Ctx()->GetCurrentNode()->AddArrayElement(node->GetValue());
                return true;
            }
            if (IsConstDicStart())
            {
                tTJSExprNode* node = NULL;
                if (!ParseConstInlineDic(&node))
                    return false;
                Ctx()->GetCurrentNode()->AddArrayElement(node->GetValue());
                return true;
            }
            return Unexpected(TJS_N("a constant value"));

        default:
            return Unexpected(TJS_N("a constant value"));
    }
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseConstInlineArray(tTJSExprNode** out)
{
    // "(" "const" ")" "[" $@27 ...
    Drop(); // '('
    Drop(); // 'const'
    if (!Expect(T_RPARENTHESIS, TJS_N("')' after 'const'")))
        return false;
    if (!Expect(T_LBRACKET, TJS_N("'[' after (const)")))
        return false;

    tTJSExprNode* node = Ctx()->MakeNP0(T_CONSTVAL);
    iTJSDispatch2* dsp = TJSCreateArrayObject();
    node->SetValue(tTJSVariant(dsp, dsp));
    dsp->Release();
    Ctx()->PushCurrentNode(node);

    bool ok = true;
    if (Peek() != T_RBRACKET)
    {
        while (true)
        {
            if (!ParseConstArrayElement())
            {
                ok = false;
                break;
            }
            if (!Accept(T_COMMA))
                break;
        }
    }

    if (ok)
        ok = Expect(T_RBRACKET, TJS_N("']' after the constant array elements"));

    if (ok)
    {
        *out = Ctx()->GetCurrentNode();
        Ctx()->PopCurrentNode();
    }
    return ok;
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseConstDicElement()
{
    tTJSToken key;
    if (!Take(T_CONSTVAL, key))
        return false;
    if (!Expect(T_COMMA, TJS_N("',' between the constant dictionary key and value")))
        return false;

    switch (Peek())
    {
        case T_MINUS:
        case T_PLUS:
        {
            int sign = Peek();
            Drop();
            tTJSToken value;
            if (!Take(T_CONSTVAL, value))
                return false;
            tTJSVariant v(Lexer->GetValue(value.Value));
            Ctx()->GetCurrentNode()->AddDictionaryElement(
                Lexer->GetValue(key.Value), sign == T_MINUS ? -v : +v);
            return true;
        }

        case T_CONSTVAL:
        {
            tjs_int value = Token().Value;
            Drop();
            Ctx()->GetCurrentNode()->AddDictionaryElement(Lexer->GetValue(key.Value),
                                                          Lexer->GetValue(value));
            return true;
        }

        case T_VOID:
            Drop();
            Ctx()->GetCurrentNode()->AddDictionaryElement(Lexer->GetValue(key.Value),
                                                          tTJSVariant());
            return true;

        case T_LPARENTHESIS:
            if (IsConstArrayStart())
            {
                tTJSExprNode* node = NULL;
                if (!ParseConstInlineArray(&node))
                    return false;
                Ctx()->GetCurrentNode()->AddDictionaryElement(Lexer->GetValue(key.Value),
                                                              node->GetValue());
                return true;
            }
            if (IsConstDicStart())
            {
                tTJSExprNode* node = NULL;
                if (!ParseConstInlineDic(&node))
                    return false;
                Ctx()->GetCurrentNode()->AddDictionaryElement(Lexer->GetValue(key.Value),
                                                              node->GetValue());
                return true;
            }
            return Unexpected(TJS_N("a constant value"));

        default:
            return Unexpected(TJS_N("a constant value"));
    }
}

//---------------------------------------------------------------------------
bool tTJSParser::ParseConstInlineDic(tTJSExprNode** out)
{
    // "(" "const" ")" "%" "[" $@28 ...
    Drop(); // '('
    Drop(); // 'const'
    if (!Expect(T_RPARENTHESIS, TJS_N("')' after 'const'")))
        return false;
    if (!Expect(T_PERCENT, TJS_N("'%' after (const)")))
        return false;
    if (!Expect(T_LBRACKET, TJS_N("'[' after '%'")))
        return false;

    tTJSExprNode* node = Ctx()->MakeNP0(T_CONSTVAL);
    iTJSDispatch2* dsp = TJSCreateDictionaryObject();
    node->SetValue(tTJSVariant(dsp, dsp));
    dsp->Release();
    Ctx()->PushCurrentNode(node);

    bool ok = true;
    if (Peek() != T_RBRACKET)
    {
        while (true)
        {
            if (!ParseConstDicElement())
            {
                ok = false;
                break;
            }
            if (!Accept(T_COMMA))
                break;
        }
    }

    if (ok)
        ok = Expect(T_RBRACKET, TJS_N("']' after the constant dictionary elements"));

    if (ok)
    {
        *out = Ctx()->GetCurrentNode();
        Ctx()->PopCurrentNode();
    }
    return ok;
}

//---------------------------------------------------------------------------
int tTJSParser::Parse()
{
    Block->PushContextStack(TJS_N("global"), ctTopLevel);
    bool ok = ParseDefList(false);
    if (!Aborted)
        Block->PopContextStack();

    return ok && !Aborted ? 0 : 1;
}

//---------------------------------------------------------------------------
int yyparse(void* pm)
{
    tTJSParser parser((tTJSScriptBlock*)pm);
    return parser.Parse();
}
//---------------------------------------------------------------------------
} // namespace TJS
//---------------------------------------------------------------------------
