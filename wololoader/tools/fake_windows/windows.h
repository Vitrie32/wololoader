// Minimal stand-in for <windows.h> so src/wololoader.h and src/script.c build
// with the system gcc in tools/test_script.c. Only what wololoader.h needs.
#pragma once
#include <stdint.h>
#define MAX_PATH 260
#define __declspec(x)
#define __stdcall
typedef int BOOL;
typedef uint32_t DWORD;
#define TRUE 1
#define FALSE 0
