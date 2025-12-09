
#ifndef __FILE_PATH_UTIL_H__
#define __FILE_PATH_UTIL_H__

#include <string>
#include <stdlib.h>

static std::string IncludeTrailingBackslash(const std::string & path)
{
	int n = path.length();

	if ( n == 0 )
		return "/";
	switch (path.c_str()[n - 1])
	{
	case '\\':
	case '/':
		return path;
	default:
		return path + '/';
	}
}

static ttstr IncludeTrailingBackslash(const ttstr& path) {
	int n = path.length();
	if (n == 0) return TJS_W("/");
	switch (path.c_str()[n - 1])
	{
	case '\\':
	case '/':
		return path;
	default:
		return path + '/';
	}
}
inline ttstr ExcludeTrailingBackslash(const ttstr& path) {
	if( path[path.length()-1] == L'\\' ) {
		return ttstr(path, path.length() - 1);
	}
	return path;
}

#endif // __FILE_PATH_UTIL_H__
