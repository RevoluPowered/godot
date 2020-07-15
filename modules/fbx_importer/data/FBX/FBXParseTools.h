#include <stdint.h>
#include <algorithm>
#include <locale>

template<class char_t>
bool IsNewLine( char_t c )
{
	return c == '\n' || c == '\r';
}

template<class char_t>
bool IsSpace( char_t c )
{
    return (c == (char_t)' ' || c == (char_t)'\t');
}

template<class char_t>
bool IsSpaceOrNewLine( char_t c )
{
	return IsNewLine(c) || IsSpace(c);
}

template<class char_t>
bool IsLineEnd( char_t c )
{
	return (c==(char_t)'\r'||c==(char_t)'\n'||c==(char_t)'\0'||c==(char_t)'\f');
}

static inline void Swap4(void* _szOut)
{
    uint8_t* const szOut = reinterpret_cast<uint8_t*>(_szOut);
    std::swap(szOut[0],szOut[3]);
    std::swap(szOut[1],szOut[2]);
}

static inline void Swap8(void* _szOut)
{
    uint8_t* const szOut = reinterpret_cast<uint8_t*>(_szOut);
    std::swap(szOut[0],szOut[7]);
    std::swap(szOut[1],szOut[6]);
    std::swap(szOut[2],szOut[5]);
    std::swap(szOut[3],szOut[4]);
}

#define AI_SWAP4(p) Swap4(&(p))
#define AI_SWAP8(p) Swap8(&(p))