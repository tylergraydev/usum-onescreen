#pragma once

// libctrpf's headers include "types.h" and expect the plugin to provide it.
// libctru already defines the same types.
#include <3ds/types.h>

// Newer libctru dropped this macro, but libctrpf 0.8 headers still use it.
#ifndef NORETURN
#define NORETURN __attribute__((noreturn))
#endif
