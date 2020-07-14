#include <stdint.h>
#include <algorithm>
#include <locale>

template<class char_t>
bool IsNewLine( char_t c )
{
	return c == '\n' || c == '\r';
}

template<class char_t>
bool IsSpaceOrNewLine( char_t c )
{
	return IsNewLine(c) || isspace(c);
}

template<class char_t>
bool IsLineEnd( char_t c )
{
	return (c==(char_t)'\r'||c==(char_t)'\n'||c==(char_t)'\0'||c==(char_t)'\f');
}