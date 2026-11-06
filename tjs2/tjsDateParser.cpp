//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Date/time string parser
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <time.h>

#include "tjsDateParser.h"

#include "tjsError.h"

namespace TJS
{
//---------------------------------------------------------------------------
// tTJSDateParser : A date/time parser class
//---------------------------------------------------------------------------
tTJSDateParser::tTJSDateParser(const tjs_char* in)
{
    Input = InputPointer = in;

    YearSet = MonthSet = MDaySet = HourSet = MinSet = SecSet = TimeZoneSet = TimeZoneOffsetSet =
        AMPMSet = false;

    for (int i = 0; i < 3; i++)
        Lookahead[i] = tToken();

    if (!Parse())
        TJS_eTJSError(TJSCannotParseDate);

    // currently no omissions is allowed except for Secs
    if (!YearSet || !MonthSet || !MDaySet || !HourSet || !MinSet)
        TJS_eTJSError(TJSCannotParseDate);
    if (!SecSet)
        SecSet = true, Sec = 0;

    // convert Timezone/TimezoneOffset to time_t representation
    if (TimeZoneSet)
    {
        // input timezone is [+/-]hhmm
        bool sign = TimeZone < 0 ? true : false;
        if (sign)
            TimeZone = -TimeZone;
        TimeZone = (int)(TimeZone / 100) * 60 * 60 + (TimeZone % 100) * 60;
        if (sign)
            TimeZone = -TimeZone;
    }
    if (TimeZoneOffsetSet)
    {
        // input timezone is [+/-]hhmm
        bool sign = TimeZoneOffset < 0 ? true : false;
        if (sign)
            TimeZoneOffset = -TimeZoneOffset;
        TimeZoneOffset = (int)(TimeZoneOffset / 100) * 60 * 60 + (TimeZoneOffset % 100) * 60;
        if (sign)
            TimeZoneOffset = -TimeZoneOffset;
    }

    // Timezone is default system timezone when timezone is omitted.
    if (!TimeZoneSet && !TimeZoneOffsetSet)
    {
        TimeZoneSet = true;
        TimeZone = -TJS_timezone();
    }

    // Adjust AM/PM
    if (AMPMSet && AMPM)
        Hour += 12;

    // build tm structure
    struct tm stm;
    memset(&stm, 0, sizeof(stm));
    stm.tm_year = Year - 1900;
    stm.tm_mon = Month;
    stm.tm_mday = MDay;
    stm.tm_hour = Hour;
    stm.tm_min = Min;
    stm.tm_sec = Sec;

    time_t tmv = mktime(&stm);
    if (tmv == -1)
        TJS_eTJSError(TJSInvalidValueForTimestamp);

    // adjust time zone
    tmv -= TJS_timezone();
    tjs_int tz = 0;
    if (TimeZoneSet)
        tz += TimeZone;
    if (TimeZoneOffsetSet)
        tz += TimeZoneOffset;
    tmv -= tz;

    // store result
    Time = (tjs_int64)tmv * 1000;
}
//---------------------------------------------------------------------------
tTJSDateParser::~tTJSDateParser()
{
}
//---------------------------------------------------------------------------
tjs_int64 tTJSDateParser::GetTime()
{
    return Time;
}
//---------------------------------------------------------------------------
void tTJSDateParser::Next()
{
    Peek(0);
    Lookahead[0] = Lookahead[1];
    Lookahead[1] = Lookahead[2];
    Lookahead[2] = tToken();
}
//---------------------------------------------------------------------------
int tTJSDateParser::Peek(size_t depth)
{
    for (size_t i = 0; i <= depth; i++)
    {
        if (Lookahead[i].Fetched)
            continue;
        tTJSDateLexValue value;
        value.val = 0;
        int id = Lex(&value);
        if (id < 0)
            id = 0;
        Lookahead[i].Id = id;
        Lookahead[i].Value = value.val;
        Lookahead[i].Fetched = true;
    }
    return Lookahead[depth].Id;
}
//---------------------------------------------------------------------------
tjs_int32 tTJSDateParser::PeekValue(size_t depth)
{
    Peek(depth);
    return Lookahead[depth].Value;
}
//---------------------------------------------------------------------------
bool tTJSDateParser::IsTimeStart()
{
    int id = Peek(0);
    if (id == DP_AM || id == DP_PM)
        return true;
    if (id == DP_NUMBER)
        return Peek(1) == TJS_N(':');
    return false;
}
//---------------------------------------------------------------------------
bool tTJSDateParser::ParseTime()
{
    bool ampm_before = false;
    int id = Peek(0);
    if (id == DP_AM || id == DP_PM)
    {
        ampm_before = true;
        SetAMPM(id == DP_PM);
        Next();
    }

    if (Peek(0) != DP_NUMBER)
        return false;
    SetHours(PeekValue(0));
    Next();

    if (Peek(0) != TJS_N(':'))
        return false;
    Next();

    if (Peek(0) != DP_NUMBER)
        return false;
    SetMin(PeekValue(0));
    Next();

    if (Peek(0) == TJS_N(':'))
    {
        Next();
        if (Peek(0) != DP_NUMBER)
            return false;
        SetSec(PeekValue(0));
        Next();

        if (Peek(0) == TJS_N('.'))
        {
            Next();
            if (Peek(0) != DP_NUMBER)
                return false;
            Next(); // sub-seconds are not represented in struct tm
        }
    }
    else
    {
        SetSec(0);
    }

    if (!ampm_before)
    {
        id = Peek(0);
        if (id == DP_AM || id == DP_PM)
        {
            SetAMPM(id == DP_PM);
            Next();
        }
    }

    return true;
}
//---------------------------------------------------------------------------
bool tTJSDateParser::ParseTimeZone()
{
    if (Peek(0) == DP_TZ)
    {
        SetTimeZone(PeekValue(0));
        Next();
    }

    int id = Peek(0);
    if (id == TJS_N('+') || id == TJS_N('-'))
    {
        int sign = id;
        Next();
        if (Peek(0) != DP_NUMBER)
            return false;
        int value = PeekValue(0);
        Next();
        SetTimeZoneOffset(sign == TJS_N('-') ? -value : value);
    }

    if (Peek(0) == TJS_N('('))
    {
        Next();
        SkipToRightParenthesis();
        if (Peek(0) != TJS_N(')'))
            return false;
        Next();
    }

    return true;
}
//---------------------------------------------------------------------------
bool tTJSDateParser::Parse()
{
    // wday_omittable
    if (Peek(0) == DP_WDAY)
    {
        Next();
        if (Peek(0) == TJS_N(','))
            Next();
    }

    bool parsed = false;
    int id = Peek(0);

    if (id == DP_NUMBER)
    {
        int first = PeekValue(0);
        Next();
        int sep = Peek(0);

        if (sep == DP_MONTH)
        {
            // day month [ '-' ] year time  /  day month [ '-' ] time year
            int month = PeekValue(0);
            Next();
            SetMDay(first);
            SetMonth(month);
            if (Peek(0) == TJS_N('-'))
                Next();

            if (IsTimeStart())
            {
                if (!ParseTime())
                    return false;
                if (Peek(0) != DP_NUMBER)
                    return false;
                SetYear(PeekValue(0));
                Next();
            }
            else if (Peek(0) == DP_NUMBER)
            {
                SetYear(PeekValue(0));
                Next();
                if (!ParseTime())
                    return false;
            }
            else
            {
                return false;
            }
            parsed = true;
        }
        else if (sep == TJS_N('-') || sep == TJS_N('/'))
        {
            int separator = sep;
            Next();

            if (Peek(0) == DP_MONTH)
            {
                // day '-' month [ '-' ] year time
                if (separator != TJS_N('-'))
                    return false;
                int month = PeekValue(0);
                Next();
                SetMDay(first);
                SetMonth(month);
                if (Peek(0) == TJS_N('-'))
                    Next();

                if (IsTimeStart())
                {
                    // day '-' month time year
                    if (!ParseTime())
                        return false;
                    if (Peek(0) != DP_NUMBER)
                        return false;
                    SetYear(PeekValue(0));
                    Next();
                }
                else if (Peek(0) == DP_NUMBER)
                {
                    // day '-' month [ '-' ] year time
                    SetYear(PeekValue(0));
                    Next();
                    if (!ParseTime())
                        return false;
                }
                else
                {
                    return false;
                }
                parsed = true;
            }
            else if (Peek(0) == DP_NUMBER)
            {
                // year ('-'|'/') month ('-'|'/') day time
                int month = PeekValue(0);
                Next();
                int sep2 = Peek(0);
                if (sep2 != TJS_N('-') && sep2 != TJS_N('/'))
                    return false;
                Next();
                if (Peek(0) != DP_NUMBER)
                    return false;
                int day = PeekValue(0);
                Next();
                SetYear(first);
                SetMonth(month - 1);
                SetMDay(day);
                if (!ParseTime())
                    return false;
                parsed = true;
            }
        }
    }
    else if (id == DP_MONTH)
    {
        int month = PeekValue(0);
        Next();
        SetMonth(month);

        if (Peek(0) == TJS_N('-'))
            Next();
        if (Peek(0) != DP_NUMBER)
            return false;
        SetMDay(PeekValue(0));
        Next();

        if (Peek(0) == TJS_N('-'))
        {
            // month '-' day '-' year time
            Next();
            if (Peek(0) != DP_NUMBER)
                return false;
            SetYear(PeekValue(0));
            Next();
            if (!ParseTime())
                return false;
        }
        else if (IsTimeStart())
        {
            // month [ '-' ] day time year
            if (!ParseTime())
                return false;
            if (Peek(0) != DP_NUMBER)
                return false;
            SetYear(PeekValue(0));
            Next();
        }
        else if (Peek(0) == DP_NUMBER)
        {
            // month [ '-' ] day year time
            SetYear(PeekValue(0));
            Next();
            if (!ParseTime())
                return false;
        }
        else
        {
            return false;
        }
        parsed = true;
    }

    if (!parsed)
        return false;
    if (!ParseTimeZone())
        return false;
    return Peek(0) == 0;
}
//---------------------------------------------------------------------------
int tTJSDateParser::Lex(tTJSDateLexValue* yylex)
{
    if (*InputPointer == 0)
        return -1;

    while (*InputPointer && TJS_iswspace(InputPointer))
        InputPointer++;

    if (*InputPointer == 0)
        return -1;

    if (TJS_iswdigit(InputPointer))
    {
        tjs_int32 val = *InputPointer - TJS_N('0');
        InputPointer++;
        while (TJS_iswdigit(InputPointer))
        {
            val *= 10;
            val += *InputPointer - TJS_N('0');
            InputPointer++;
        }
        yylex->val = val;
        return DP_NUMBER;
    }

#include "tjsDateWordMap.cc"

    int n = (int)*(InputPointer++);
    if (n >= TJS_N('A') && n <= TJS_N('Z'))
        n += TJS_N('a') - TJS_N('A');
    return n;
}
//---------------------------------------------------------------------------
void tTJSDateParser::SkipToRightParenthesis()
{
    // skip InputPointer to ')'
    while (*InputPointer && *InputPointer != ')')
        InputPointer++;
}
//---------------------------------------------------------------------------
void tTJSDateParser::SetMDay(int v)
{
    MDaySet = true;
    MDay = v;
}
//---------------------------------------------------------------------------
void tTJSDateParser::SetMonth(int v)
{
    MonthSet = true;
    Month = v;
}
//---------------------------------------------------------------------------
void tTJSDateParser::SetYear(int v)
{
    if (v < 100)
    {
        if (v <= 50)
            v = 2000 + v;
        else
            v = 1900 + v;
    }
    YearSet = true;
    Year = v;
}
//---------------------------------------------------------------------------
void tTJSDateParser::SetHours(int v)
{
    HourSet = true;
    Hour = v;
}
//---------------------------------------------------------------------------
void tTJSDateParser::SetMin(int v)
{
    MinSet = true;
    Min = v;
}
//---------------------------------------------------------------------------
void tTJSDateParser::SetSec(int v)
{
    SecSet = true;
    Sec = v;
}
//---------------------------------------------------------------------------
void tTJSDateParser::SetAMPM(bool is_pm)
{
    AMPMSet = true;
    AMPM = is_pm;
}
//---------------------------------------------------------------------------
void tTJSDateParser::SetTimeZone(int v)
{
    TimeZoneSet = true;
    TimeZone = v;
}
//---------------------------------------------------------------------------
void tTJSDateParser::SetTimeZoneOffset(int v)
{
    TimeZoneOffsetSet = true;
    TimeZoneOffset = v;
}
//---------------------------------------------------------------------------

} // namespace TJS
