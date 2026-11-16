# Jump stubs forwarding every version.dll export to the real system DLL.
# The pointer table is filled by proxy_init() in proxy.c.
.intel_syntax noprefix
.macro STUB name, idx
  .globl _wl_\name
  _wl_\name:
    jmp dword ptr [_wl_real_procs + \idx*4]
.endm
.text
STUB GetFileVersionInfoA, 0
STUB GetFileVersionInfoByHandle, 1
STUB GetFileVersionInfoExA, 2
STUB GetFileVersionInfoExW, 3
STUB GetFileVersionInfoSizeA, 4
STUB GetFileVersionInfoSizeExA, 5
STUB GetFileVersionInfoSizeExW, 6
STUB GetFileVersionInfoSizeW, 7
STUB GetFileVersionInfoW, 8
STUB VerFindFileA, 9
STUB VerFindFileW, 10
STUB VerInstallFileA, 11
STUB VerInstallFileW, 12
STUB VerLanguageNameA, 13
STUB VerLanguageNameW, 14
STUB VerQueryValueA, 15
STUB VerQueryValueW, 16
