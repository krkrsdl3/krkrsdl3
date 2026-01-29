//---------------------------------------------------------------------------
/*
	TJS2 Script Engine
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// configuration
//---------------------------------------------------------------------------

#include "tjsCommHead.h"

#include "Log.h"

#include <codecvt>

/*
 * core/utils/cp932_uni.cpp
 * core/utils/uni_cp932.cpp
 * を一緒にリンクしてください。
 * CP932(ShiftJIS) と Unicode 変換に使用しています。
 * Win32 APIの同等の関数は互換性等の問題があることやマルチプラットフォームの足かせとなる
 * ため使用が中止されました。
 */

static int utf8_mbtowc(tjs_char* pwc, const unsigned char* s, int n)
{
	unsigned char c = s[0];

	if (c < 0x80) {
		*pwc = c;
		return 1;
	}
	else if (c < 0xc2) {
		return -1;
	}
	else if (c < 0xe0) {
		if (n < 2)
			return -1;
		if (!((s[1] ^ 0x80) < 0x40))
			return -1;
		*pwc = ((tjs_char)(c & 0x1f) << 6)
			| (tjs_char)(s[1] ^ 0x80);
		return 2;
	}
	else if (c < 0xf0) {
		if (n < 3)
			return -1;
		if (!((s[1] ^ 0x80) < 0x40 && (s[2] ^ 0x80) < 0x40
			&& (c >= 0xe1 || s[1] >= 0xa0)))
			return -1;
		*pwc = ((tjs_char)(c & 0x0f) << 12)
			| ((tjs_char)(s[1] ^ 0x80) << 6)
			| (tjs_char)(s[2] ^ 0x80);
		return 3;
	}
	else
		return -1;
}

static int utf8_wctomb(unsigned char* r, tjs_char wc, int n)
{
	int count;
	if (wc < 0x80)
		count = 1;
	else if (wc < 0x800)
		count = 2;
	else if (wc < 0x10000)
		count = 3;
	// 	else if (wc < 0x200000)
	// 		count = 4;
	// 	else if (wc < 0x4000000)
	// 		count = 5;
	// 	else if (wc <= 0x7fffffff)
	// 		count = 6;
	else
		return -1;
	if (n < count)
		return -2;
	switch (count) { /* note: code falls through cases! */
		// 	case 6: r[5] = 0x80 | (wc & 0x3f); wc = wc >> 6; wc |= 0x4000000;
		// 	case 5: r[4] = 0x80 | (wc & 0x3f); wc = wc >> 6; wc |= 0x200000;
		// 	case 4: r[3] = 0x80 | (wc & 0x3f); wc = wc >> 6; wc |= 0x10000;
	case 3: r[2] = 0x80 | (wc & 0x3f); wc = wc >> 6; wc |= 0x800;
	case 2: r[1] = 0x80 | (wc & 0x3f); wc = wc >> 6; wc |= 0xc0;
	case 1: r[0] = (unsigned char)wc;
	}
	return count;
}

namespace TJS
{
//---------------------------------------------------------------------------
// debug support
//---------------------------------------------------------------------------
#if TJS_DEBUG_PROFILE_TIME
	tjs_uint TJSGetTickCount()
	{
		return GetTickCount();
	}
#endif
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// some wchar_t support functions
//---------------------------------------------------------------------------
tjs_int TJS_atoi(const tjs_char* s)
{
	int r = 0;
	bool sign = false;
	while (*s && *s <= 0x20) s++; // skip spaces
	if (!*s) return 0;
	if (*s == TJS_W('-'))
	{
		sign = true;
		s++;
		while (*s && *s <= 0x20) s++; // skip spaces
		if (!*s) return 0;
	}

	while (*s >= TJS_W('0') && *s <= TJS_W('9'))
	{
		r *= 10;
		r += *s - TJS_W('0');
		s++;
	}
	if (sign) r = -r;
	return r;
}

tjs_int64 TJS_atoll(const tjs_char* s)
{
	tjs_int64 r = 0;
	bool sign = false;
	while (*s && *s <= 0x20) s++; // skip spaces
	if (!*s) return 0;
	if (*s == TJS_W('-'))
	{
		sign = true;
		s++;
		while (*s && *s <= 0x20) s++; // skip spaces
		if (!*s) return 0;
	}

	while (*s >= TJS_W('0') && *s <= TJS_W('9'))
	{
		r *= 10;
		r += *s - TJS_W('0');
		s++;
	}
	if (sign) r = -r;
	return r;
}

tjs_char* TJS_int_to_str(tjs_int value, tjs_char* string)
{
	tjs_char* ostring = string;

	if (value < 0) *(string++) = TJS_W('-'), value = -value;

	tjs_char buf[40];

	tjs_char* p = buf;

	do
	{
		*(p++) = (value % 10) + TJS_W('0');
		value /= 10;
	} while (value);

	p--;
	while (buf <= p) *(string++) = *(p--);
	*string = 0;

	return ostring;
}

tjs_char* TJS_tTVInt_to_str(tjs_int64 value, tjs_char* string)
{
	if (value == TJS_UI64_VAL(0x8000000000000000))
	{
		// this is a special number which we must avoid normal conversion
		TJS_strcpy(string, TJS_W("-9223372036854775808"));
		return string;
	}

	tjs_char* ostring = string;

	if (value < 0) *(string++) = TJS_W('-'), value = -value;

	tjs_char buf[40];

	tjs_char* p = buf;

	do
	{
		*(p++) = (value % 10) + TJS_W('0');
		value /= 10;
	} while (value);

	p--;
	while (buf <= p) *(string++) = *(p--);
	*string = 0;

	return ostring;
}

tjs_int TJS_strnicmp(const tjs_char* s1, const tjs_char* s2,
	size_t maxlen)
{
	while (maxlen--)
	{
		if (*s1 == TJS_W('\0')) return (*s2 == TJS_W('\0')) ? 0 : -1;
		if (*s2 == TJS_W('\0')) return (*s1 == TJS_W('\0')) ? 0 : 1;
		if (*s1 < *s2) return -1;
		if (*s1 > *s2) return 1;
		s1++;
		s2++;
	}

	return 0;
}

tjs_int TJS_stricmp(const tjs_char* s1, const tjs_char* s2)
{
	// we only support basic alphabets
	// fixme: complete alphabets support

	for (;;)
	{
		tjs_char c1 = *s1, c2 = *s2;
		if (c1 >= TJS_W('a') && c1 <= TJS_W('z')) c1 += TJS_W('Z') - TJS_W('z');
		if (c2 >= TJS_W('a') && c2 <= TJS_W('z')) c2 += TJS_W('Z') - TJS_W('z');
		if (c1 == TJS_W('\0')) return (c2 == TJS_W('\0')) ? 0 : -1;
		if (c2 == TJS_W('\0')) return (c1 == TJS_W('\0')) ? 0 : 1;
		if (c1 < c2) return -1;
		if (c1 > c2) return 1;
		s1++;
		s2++;
	}
}

void TJS_strcpy_maxlen(tjs_char* d, const tjs_char* s, size_t len)
{
	tjs_char ch;
	len++;
	while ((ch = *s) != 0 && --len) *(d++) = ch, s++;
	*d = 0;
}

void TJS_strcpy(tjs_char* d, const tjs_char* s)
{
	tjs_char ch;
	while ((ch = *s) != 0) *(d++) = ch, s++;
	*d = 0;
}

size_t TJS_strlen(const tjs_char* d)
{
	const tjs_char* p = d;
	while (*d) d++;
	return d - p;
}

tjs_int TJS_sprintf(tjs_char* s, const tjs_char* format, ...)
{
	tjs_int r;
	va_list param;
	va_start(param, format);
	r = TJS_vsnprintf(s, INT_MAX, format, param);
	va_end(param);
	return r;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// Debug_Logout function
//---------------------------------------------------------------------------
void TJS_debug_out(const tjs_char* format, ...)
{
	tjs_int r;
	va_list param;
	va_start(param, format);
	tjs_char buf[256];
	r = TJS_vsnprintf(buf, 256, format, param);
	va_end(param);
	TVPConsoleLog(buf);
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
#define TJS_MB_MAX_CHARLEN 2
//---------------------------------------------------------------------------
size_t TJS_mbstowcs(tjs_char* pwcs, const tjs_nchar* s, size_t n)
{
	if (!s) return -1;
	if (pwcs && n == 0) return 0;

	tjs_char wc;
	size_t count = 0;
	int cl;
	if (!pwcs) {
		n = strlen(s);
		while (*s) {
			cl = utf8_mbtowc(&wc, (const unsigned char*)s, (int)n);
			if (cl <= 0)
				break;
			s += cl;
			n -= cl;
			++count;
		}
	}
	else {
		tjs_char* pwcsend = pwcs + n;
		n = strlen(s);
		while (*s && pwcs < pwcsend) {
			cl = utf8_mbtowc(&wc, (const unsigned char*)s, (int)n);
			if (cl <= 0)
				return -1;
			s += cl;
			n -= cl;
			*pwcs++ = wc;
			++count;
		}
	}
	return count;
}

size_t TJS_wcstombs(tjs_nchar* s, const tjs_char* pwcs, size_t n)
{
	if (!pwcs) return -1;
	if (s && !n) return 0;

	int cl;
	if (!s) {
		unsigned char tmp[6];
		size_t count = 0;
		while (*pwcs) {
			cl = utf8_wctomb(tmp, *pwcs, 6);
			if (cl <= 0)
				return -1;
			pwcs++;
			count += cl;
		}
		return count;
	}
	else {
		tjs_nchar* d = s;
		while (*pwcs && n > 0) {
			cl = utf8_wctomb((unsigned char*)d, *pwcs, (int)n);
			if (cl <= 0)
				return -1;
			n -= cl;
			d += cl;
			pwcs++;
		}
		return d - s;
	}
}
// 使われていないようなので未確認注意
int TJS_mbtowc(tjs_char* pwc, const tjs_nchar* s, size_t n)
{
	if (!s || !n) return 0;

	if (*s == 0)
	{
		if (pwc) *pwc = 0;
		return 0;
	}
	tjs_char wc;
	int ret = utf8_mbtowc(&wc, (const unsigned char*)s, (int)n);
	if (ret >= 0) {
		*pwc = wc;
	}
	return ret;
}
// 使われていないようなので未確認注意
int TJS_wctomb(tjs_nchar* s, tjs_char wc)
{
	if (!s) return 0;
	tjs_char tmp[2] = { wc, 0 };
	return utf8_wctomb((unsigned char*)s, wc, 2);
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// tTJSNarrowStringHolder
//---------------------------------------------------------------------------
tTJSNarrowStringHolder::tTJSNarrowStringHolder(const tjs_char* wide)
{
	int n;
	if (!wide)
		n = -1;
	else
		n = (int)TJS_wcstombs(NULL, wide, 0);

	if (n == -1)
	{
		Buf = NULL;
		Allocated = false;
		return;
	}
	Buf = new tjs_nchar[n + 1];
	Allocated = true;
	Buf[TJS_wcstombs(Buf, wide, n)] = 0;
}

tTJSNarrowStringHolder::~tTJSNarrowStringHolder()
{
	if (Allocated) delete[] Buf;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// native debugger break point
//---------------------------------------------------------------------------
void TJSNativeDebuggerBreak()
{
	// This function is to be called mostly when the "debugger" TJS statement is
	// executed.
	// Step you debbuger back to the the caller, and continue debugging.
	// Do not use "debugger" statement unless you run the program under the native
	// debugger, or the program may cause an unhandled debugger breakpoint
	// exception.

#if defined(__WIN32__)
#if defined(_M_IX86)
#ifdef __BORLANDC__
	__emit__(0xcc); // int 3 (Raise debugger breakpoint exception)
#else
	_asm _emit 0xcc; // int 3 (Raise debugger breakpoint exception)
#endif
#else
	__debugbreak();
#endif
#endif
}

//---------------------------------------------------------------------------
// FPU control
//---------------------------------------------------------------------------
#if defined(__WIN32__) && !defined(__GNUC__)
static unsigned int TJSDefaultFPUCW = 0;
static unsigned int TJSNewFPUCW = 0;
static unsigned int TJSDefaultMMCW = 0;
static bool TJSFPUInit = false;
#endif
// FPU例外をマスク
void TJSSetFPUE()
{
#if defined(__WIN32__) && !defined(__GNUC__)
	if (!TJSFPUInit)
	{
		TJSFPUInit = true;
#if defined(_M_X64)
		TJSDefaultMMCW = _MM_GET_EXCEPTION_MASK();
#else
		TJSDefaultFPUCW = _control87(0, 0);

#ifdef _MSC_VER
		TJSNewFPUCW = _control87(MCW_EM, MCW_EM);
#else
		_default87 = TJSNewFPUCW = _control87(MCW_EM, MCW_EM);
#endif	// _MSC_VER
#endif	// _M_X64
	}

#if defined(_M_X64)
	_MM_SET_EXCEPTION_MASK(_MM_MASK_INVALID | _MM_MASK_DIV_ZERO | _MM_MASK_DENORM | _MM_MASK_OVERFLOW | _MM_MASK_UNDERFLOW | _MM_MASK_INEXACT);
#else
	//	_fpreset();
	_control87(TJSNewFPUCW, 0xffff);
#endif
#endif	// defined(__WIN32__) && !defined(__GNUC__)

}
// 例外マスクを解除し元に戻す
void TJSRestoreFPUE()
{
#if defined(__WIN32__) && !defined(__GNUC__)
	if (!TJSFPUInit) return;
#if defined(_M_X64)
	_MM_SET_EXCEPTION_MASK(TJSDefaultMMCW);
#else
	_control87(TJSDefaultFPUCW, 0xffff);
#endif
#endif
}
//---------------------------------------------------------------------------



int TJS_strcmp(const tjs_char* src, const tjs_char* dst)
{
	int ret = 0;

	while (!(ret = (int)(*src - *dst)) && *dst)
		++src, ++dst;

	if (ret < 0)
		ret = -1;
	else if (ret > 0)
		ret = 1;

	return(ret);
}

int TJS_strncmp( const tjs_char* first, const tjs_char* last, size_t count)
{
	if (!count)
		return(0);

	while (--count && *first && *first == *last)
	{
		first++;
		last++;
	}

	return((int)(*first - *last));
}

tjs_char* TJS_strncpy(tjs_char* dest, const tjs_char* source, size_t count)
{
	tjs_char* start = dest;

	while (count && (*dest++ = *source++))    /* copy string */
		count--;

	if (count)                              /* pad out with zeroes */
		while (--count)
			*dest++ = L'\0';

	return(start);
}

tjs_char* TJS_strcat(tjs_char* dst, const tjs_char* src)
{
	tjs_char* cp = dst;

	while (*cp)
		cp++;                   /* find end of dst */

	while ((*cp++ = *src++));       /* Copy src to end of dst */

	return(dst);                  /* return dst */

}

tjs_char* TJS_strstr(const tjs_char* wcs1, const tjs_char* wcs2)
{
	tjs_char* cp = (tjs_char*)wcs1;
	tjs_char* s1, * s2;

	if (!*wcs2)
		return (tjs_char*)wcs1;

	while (*cp)
	{
		s1 = cp;
		s2 = (tjs_char*)wcs2;

		while (*s1 && *s2 && !(*s1 - *s2))
			s1++, s2++;

		if (!*s2)
			return(cp);

		cp++;
	}

	return(NULL);
}

tjs_char* TJS_strchr(const tjs_char* string, tjs_char ch)
{
	while (*string && *string != (tjs_char)ch)
		string++;

	if (*string == (tjs_char)ch)
		return((tjs_char*)string);
	return(NULL);
}

void* TJS_malloc(size_t len)
{
	char* ret = (char*)malloc(len + sizeof(size_t));
	if (!ret) return nullptr;
	*(size_t*)ret = len; // embed size
	return ret + sizeof(size_t);
}

void* TJS_realloc(void* buf, size_t len)
{
	if (!buf) return TJS_malloc(len);
	// compare embeded size
	size_t* ptr = (size_t*)((char*)buf - sizeof(size_t));
	if (*ptr >= len) return buf; // still adequate
	char* ret = (char*)TJS_malloc(len);
	if (!ret) return nullptr;
	memcpy(ret, ptr + 1, *ptr);
	TJS_free(buf);
	return ret;
}

void TJS_free(void* buf)
{
	free((char*)buf - sizeof(size_t));
}

tjs_char* TJS_strrchr(const tjs_char* s, int c)
{
	tjs_char* ret = 0;
	do {
		if (*s == (char)c)
			ret = (tjs_char*)s;
	} while (*s++);
	return ret;
}

double TJS_strtod(const tjs_char* string, tjs_char** endPtr)
{
    int sign, expSign = false;
    double fraction, dblExp;
    const double* d;
    const tjs_char* p;
    int c;
    int exp = 0;		/* Exponent read from "EX" field. */
    int fracExp = 0;		/* Exponent that derives from the fractional
                            * part.  Under normal circumstatnces, it is
                            * the negative of the number of digits in F.
                            * However, if I is very long, the last digits
                            * of I get dropped (otherwise a long I with a
                            * large negative exponent could cause an
                            * unnecessary overflow on I alone).  In this
                            * case, fracExp is incremented one for each
                            * dropped digit. */
    int mantSize;		/* Number of digits in mantissa. */
    int decPt;			/* Number of mantissa digits BEFORE decimal
                        * point. */
    const tjs_char* pExp;		/* Temporarily holds location of exponent
                            * in string. */
    static const int maxExponent = 511;
    static const double powersOf10[] = {	/* Table giving binary powers of 10.  Entry */
        10.,			/* is 10^2^i.  Used to convert decimal */
        100.,			/* exponents into floating-point numbers. */
        1.0e4,
        1.0e8,
        1.0e16,
        1.0e32,
        1.0e64,
        1.0e128,
        1.0e256
    };
    /*
    * Strip off leading blanks and check for a sign.
    */

    p = string;
    while (isspace((*p))) {
        p += 1;
    }
    if (*p == '-') {
        sign = true;
        p += 1;
    }
    else {
        if (*p == '+') {
            p += 1;
        }
        sign = false;
    }

    /*
    * Count the number of digits in the mantissa (including the decimal
    * point), and also locate the decimal point.
    */

    decPt = -1;
    for (mantSize = 0;; mantSize += 1)
    {
        c = *p;
        if (!isdigit(c)) {
            if ((c != '.') || (decPt >= 0)) {
                break;
            }
            decPt = mantSize;
        }
        p += 1;
    }

    /*
    * Now suck up the digits in the mantissa.  Use two integers to
    * collect 9 digits each (this is faster than using floating-point).
    * If the mantissa has more than 18 digits, ignore the extras, since
    * they can't affect the value anyway.
    */

    pExp = p;
    p -= mantSize;
    if (decPt < 0) {
        decPt = mantSize;
    }
    else {
        mantSize -= 1;			/* One of the digits was the point. */
    }
    if (mantSize > 48) {
        fracExp = decPt - 48;
        mantSize = 48;
    }
    else {
        fracExp = decPt - mantSize;
    }
    if (mantSize == 0) {
        fraction = 0.0;
        p = string;
        goto done;
    }
    else {
        int frac1, frac2;
        frac1 = 0;
        for (; mantSize > 9; mantSize -= 1)
        {
            c = *p;
            p += 1;
            if (c == '.') {
                c = *p;
                p += 1;
            }
            frac1 = 10 * frac1 + (c - '0');
        }
        frac2 = 0;
        for (; mantSize > 0; mantSize -= 1)
        {
            c = *p;
            p += 1;
            if (c == '.') {
                c = *p;
                p += 1;
            }
            frac2 = 10 * frac2 + (c - '0');
        }
        fraction = (1.0e9 * frac1) + frac2;
    }

    /*
    * Skim off the exponent.
    */

    p = pExp;
    if ((*p == 'E') || (*p == 'e')) {
        p += 1;
        if (*p == '-') {
            expSign = true;
            p += 1;
        }
        else {
            if (*p == '+') {
                p += 1;
            }
            expSign = false;
        }
        if (!isdigit((*p))) {
            p = pExp;
            goto done;
        }
        while (isdigit((*p))) {
            exp = exp * 10 + (*p - '0');
            p += 1;
        }
    }
    if (expSign) {
        exp = fracExp - exp;
    }
    else {
        exp = fracExp + exp;
    }

    /*
    * Generate a floating-point number that represents the exponent.
    * Do this by processing the exponent one bit at a time to combine
    * many powers of 2 of 10. Then combine the exponent with the
    * fraction.
    */

    if (exp < 0) {
        expSign = true;
        exp = -exp;
    }
    else {
        expSign = false;
    }
    if (exp > maxExponent) {
        exp = maxExponent;
        errno = ERANGE;
    }
    dblExp = 1.0;
    for (d = powersOf10; exp != 0; exp >>= 1, d += 1) {
        if (exp & 01) {
            dblExp *= *d;
        }
    }
    if (expSign) {
        fraction /= dblExp;
    }
    else {
        fraction *= dblExp;
    }

done:
    if (endPtr != NULL) {
        *endPtr = (tjs_char*)p;
    }

    if (sign) {
        return -fraction;
    }
    return fraction;
}

extern std::string TVP_codecvt_utf8_utf16(const tjs_char* indata);
extern std::u16string TVP_codecvt_utf16_utf8(const char* indata);
size_t TJS_strftime(tjs_char* wstring, size_t maxsize, const tjs_char* wformat, const tm* timeptr)
{
    std::string formatStr = TVP_codecvt_utf8_utf16(wformat);
    char buffer[256];
    strftime(buffer, sizeof(buffer), formatStr.c_str(), timeptr);
    std::u16string ret = TVP_codecvt_utf16_utf8(buffer);
    size_t retLen = ret.size() > maxsize ? maxsize : ret.size();
    memcpy(wstring, ret.data(), retLen);
    return retLen;
}

int TJS_vsnprintf(tjs_char* string, size_t count, const tjs_char* format, va_list ap)
{
    try {
        // 字符体系会改的，先这样凑合着用吧
        std::string format_utf8 = TVP_codecvt_utf8_utf16(format);
        std::string buffer(count * 4, '\0');
        va_list ap_copy;
        va_copy(ap_copy, ap);
        int result = std::vsnprintf(buffer.data(), buffer.size(), format_utf8.c_str(), ap_copy);
        va_end(ap_copy);
        if (result < 0 || static_cast<size_t>(result) >= buffer.size()) {
            return 0;
        }
        std::u16string result_utf16 = TVP_codecvt_utf16_utf8(buffer.c_str());
        size_t copy_len = std::min<size_t>(result_utf16.size(), count - 1);
        std::copy_n(result_utf16.c_str(), copy_len, string);
        string[copy_len] = u'\0';
        return static_cast<int>(result_utf16.size());
    } catch (...) {
        return 0;
    }
}

tjs_int TJS_snprintf(tjs_char* s, size_t count, const tjs_char* format, ...)
{
    tjs_int r;
    va_list param;
    va_start(param, format);
    r = TJS_vsnprintf(s, count, format, param);
    va_end(param);
    return r;
}
}
