//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Date/time string parser
//---------------------------------------------------------------------------
#ifndef tjsDateParserH
#define tjsDateParserH

#include "tjsTypes.h"

#include <vector>

namespace TJS
{
//---------------------------------------------------------------------------
// Tokens of the date/time language (values kept from the old bison grammar).
//---------------------------------------------------------------------------
enum tTJSDateToken
{
    DP_AM = 258,
    DP_PM = 259,
    DP_NUMBER = 260,
    DP_MONTH = 261,
    DP_WDAY = 262,
    DP_TZ = 263
};
//---------------------------------------------------------------------------
struct tTJSDateLexValue
{
    tjs_int32 val;
};
//---------------------------------------------------------------------------
// tTJSDateParser : A date/time parser class
//---------------------------------------------------------------------------
class tTJSDateParser
{
    bool YearSet;
    int Year;

    bool MonthSet;
    int Month;

    bool MDaySet;
    int MDay;

    bool HourSet;
    int Hour;

    bool MinSet;
    int Min;

    bool SecSet;
    int Sec;

    bool AMPMSet;
    bool AMPM; // pm:true am:false

    bool TimeZoneSet;
    int TimeZone;

    bool TimeZoneOffsetSet;
    int TimeZoneOffset;

    const tjs_char* Input;
    const tjs_char* InputPointer;

    tjs_int64 Time; // time from 1970-01-01 00:00:00.00 GMT

    struct tToken
    {
        int Id;
        tjs_int32 Value;
        bool Fetched;
        tToken() : Id(0), Value(0), Fetched(false) {}
    };
    tToken Lookahead[3];

public:
    tTJSDateParser(const tjs_char* in);
    ~tTJSDateParser();

    tjs_int64 GetTime();

private:
    int Lex(tTJSDateLexValue* value);

    tjs_int32 PeekValue(size_t depth);
    int Peek(size_t depth = 0);
    void Next();

    bool Parse();
    bool ParseTime();
    bool ParseTimeZone();
    bool IsTimeStart();

    void SkipToRightParenthesis();

    void SetMDay(int v);
    void SetMonth(int v);
    void SetYear(int v);
    void SetHours(int v);
    void SetMin(int v);
    void SetSec(int v);
    void SetAMPM(bool is_pm);
    void SetTimeZone(int v);
    void SetTimeZoneOffset(int v);
};

//---------------------------------------------------------------------------
} // namespace TJS
#endif
