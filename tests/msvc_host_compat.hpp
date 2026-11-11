#pragma once
// MSVC host builds only: the production queue uses GCC printf checking.
#if defined(_MSC_VER) && !defined(__clang__)
#define __attribute__(attributes)
#endif
