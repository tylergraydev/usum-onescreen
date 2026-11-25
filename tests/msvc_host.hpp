#pragma once
// GNU printf-format checking has no MSVC equivalent; no runtime effect.
#ifdef _MSC_VER
#define __attribute__(x)
#endif
