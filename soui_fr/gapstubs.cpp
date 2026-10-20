/*
 * soui_fr/gapstubs.cpp -- link-closing stubs for the SOUI FreeRTOS attempt.
 *
 * Every symbol here is provided by a swinx POSIX platform file on Linux/macOS
 * (sysapi.cpp / memory.cpp / fileapi.cpp / shellapi.cpp / cursoricon.cpp /
 * platform/<os>/dlghelper) that is excluded from the FreeRTOS build.  The
 * stubs prove the link CAN close; the real port moves them into the swinx
 * platform/freertos bridge with functional implementations.  All stubs fail
 * gracefully (return 0 / NULL / -1) unless a trivial correct implementation
 * exists (time wrappers around newlib).
 */
#include <windows.h>
#include <commdlg.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- module loading (Linux: sysapi.cpp, dl-backed) ------------------------
HMODULE GetModuleHandleA(LPCSTR name) { (void)name; return NULL; }
HMODULE LoadLibraryA(LPCSTR name) { (void)name; return NULL; }
BOOL FreeLibrary(HMODULE h) { (void)h; return FALSE; }
FARPROC GetProcAddress(HMODULE h, LPCSTR name) { (void)h; (void)name; return NULL; }
DWORD GetModuleFileNameA(HMODULE h, LPSTR buf, DWORD len) { (void)h; (void)buf; (void)len; return 0; }

// dlfcn bridge completion (declared in platform/freertos/dlfcn.h)
void *dlsym(void *handle, const char *symbol) { (void)handle; (void)symbol; return NULL; }
char *dlerror(void) { return NULL; }

// ---- heap (Linux: memory.cpp) ---------------------------------------------
HANDLE HeapCreate(DWORD flOptions, SIZE_T dwInitialSize, SIZE_T dwMaximumSize)
{ (void)flOptions; (void)dwInitialSize; (void)dwMaximumSize; return NULL; }
BOOL HeapDestroy(HANDLE hHeap) { (void)hHeap; return FALSE; }
SIZE_T HeapSize(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem)
{ (void)hHeap; (void)dwFlags; (void)lpMem; return 0; }
BOOL HeapValidate(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem)
{ (void)hHeap; (void)dwFlags; (void)lpMem; return FALSE; }

// ---- global/local alloc (Linux: memory.cpp) --------------------------------
HGLOBAL GlobalAlloc(UINT uFlags, SIZE_T dwBytes) { (void)uFlags; (void)dwBytes; return NULL; }
HGLOBAL GlobalFree(HGLOBAL hMem) { (void)hMem; return NULL; }
LPVOID GlobalLock(HGLOBAL hMem) { (void)hMem; return NULL; }
BOOL GlobalUnlock(HGLOBAL hMem) { (void)hMem; return FALSE; }
HLOCAL LocalAlloc(UINT uFlags, SIZE_T uBytes) { (void)uFlags; (void)uBytes; return NULL; }
HLOCAL LocalReAlloc(HLOCAL hMem, SIZE_T uBytes, UINT uFlags)
{ (void)hMem; (void)uBytes; (void)uFlags; return NULL; }

// ---- time (Linux: sysapi.cpp) ----------------------------------------------
// NOTE: winobjs.cpp (freertos) already defines GetLocalTime/GetTickCount.
uint64_t GetTickCount64(void) { return 0; }
void GetSystemTime(SYSTEMTIME *st) { (void)st; }
DWORD GetCurrentProcessId(void) { return 1; }

// time64 wrappers around newlib time/mktime
__time64_t _time64(__time64_t *t) { __time64_t v = (__time64_t)time(NULL); if (t) *t = v; return v; }
__time64_t _mktime64(const struct tm *tm) { return (__time64_t)mktime(const_cast<struct tm *>(tm)); }
time_t _mkgmtime(struct tm *tm)
{
    // newlib has no timegm; portable fallback: return failure
    (void)tm;
    return (time_t)-1;
}

// ---- file (Linux: fileapi.cpp) ----------------------------------------------
HANDLE FindFirstFileA(LPCSTR name, LPWIN32_FIND_DATAA fd) { (void)name; (void)fd; return INVALID_HANDLE_VALUE; }
BOOL FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA fd) { (void)h; (void)fd; return FALSE; }
BOOL FindClose(HANDLE h) { (void)h; return FALSE; }
DWORD GetFileAttributesA(LPCSTR name) { (void)name; return INVALID_FILE_ATTRIBUTES; }
DWORD GetFullPathNameA(LPCSTR fn, DWORD len, LPSTR buf, LPSTR *part)
{ (void)fn; (void)len; (void)buf; (void)part; return 0; }

// MSVC CRT file/time names referenced by SOUI helpers
int _open(const char *path, int oflag, ...) { (void)path; (void)oflag; return -1; }
int _unlink(const char *path) { (void)path; return -1; }
int _link(const char *oldp, const char *newp) { (void)oldp; (void)newp; return -1; }
int _stat(const char *path, struct stat *buf) { (void)path; (void)buf; return -1; }

// ---- misc CRT / entropy ------------------------------------------------------
int _getentropy(void *buf, size_t len) { (void)buf; (void)len; return -1; }

// newlib reentrant time syscalls (pulled in by clock()/time() above)
struct timeval;
struct timezone;
extern "C" int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tv;
    (void)tz;
    return -1;
}
extern "C" clock_t _times(clock_t *t) { if (t) *t = 0; return 0; }
void qsort_s(void *base, size_t nmemb, size_t size,
             int (*cmp)(void *, const void *, const void *), void *ctx)
{
    // insertion-sort fallback with the qsort_s comparator shape (ctx first)
    char *b = (char *)base;
    static char tmp[256];
    if (size > sizeof(tmp))
        return;
    for (size_t i = 1; i < nmemb; ++i)
    {
        memcpy(tmp, b + i * size, size);
        size_t j = i;
        while (j > 0 && cmp(ctx, b + (j - 1) * size, tmp) > 0)
        {
            memcpy(b + j * size, b + (j - 1) * size, size);
            --j;
        }
        memcpy(b + j * size, tmp, size);
    }
}

// ---- misc Win32 ---------------------------------------------------------------
void OutputDebugStringA(LPCSTR s) { (void)s; }
void PostThreadMessageA(uint64_t tid, UINT msg, WPARAM wp, LPARAM lp)
{ (void)tid; (void)msg; (void)wp; (void)lp; }
BOOL IsDBCSLeadByte(BYTE ch) { (void)ch; return FALSE; }

BOOL StrToIntExA(const char *str, DWORD flags, int *ret)
{
    (void)flags;
    if (!str || !ret)
        return FALSE;
    char *end = NULL;
    long v = strtol(str, &end, 0);
    if (end == str)
        return FALSE;
    *ret = (int)v;
    return TRUE;
}
BOOL StrToIntExW(const wchar_t *str, DWORD flags, int *ret)
{
    (void)flags;
    if (!str || !ret)
        return FALSE;
    wchar_t *end = NULL;
    long v = wcstol(str, &end, 0);
    if (end == str)
        return FALSE;
    *ret = (int)v;
    return TRUE;
}

// ---- IMM (Linux: XIM bridge; FreeRTOS: no IME) --------------------------------
HIMC ImmCreateContext(void) { return NULL; }
BOOL ImmDestroyContext(HIMC h) { (void)h; return FALSE; }
HIMC ImmGetContext(HWND hWnd) { (void)hWnd; return NULL; }
HIMC ImmAssociateContext(HWND hWnd, HIMC h) { (void)hWnd; (void)h; return NULL; }
BOOL ImmReleaseContext(HWND hWnd, HIMC h) { (void)hWnd; (void)h; return FALSE; }

// ---- cursor / icon / bitmap (Linux: cursoricon.cpp) ----------------------------
BOOL DestroyCursor(HCURSOR h) { (void)h; return FALSE; }
HBITMAP LoadBitmapA(HINSTANCE h, LPCSTR name) { (void)h; (void)name; return NULL; }
HICON CreateIconFromResource(PBYTE bits, DWORD cb, BOOL fIcon, DWORD ver)
{ (void)bits; (void)cb; (void)fIcon; (void)ver; return NULL; }

// ---- drag-drop / shell / dialogs (platform dlghelper) ---------------------------
HRESULT RegisterDragDrop(HWND hWnd, LPDROPTARGET pTarget) { (void)hWnd; (void)pTarget; return E_FAIL; }
BOOL ShellExecuteA(HWND hwnd, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, INT show)
{ (void)hwnd; (void)op; (void)file; (void)params; (void)dir; (void)show; return FALSE; }

#ifdef __cplusplus
}
#endif

// common-dialog platform hooks (swinx/src/cmmmdlg.cpp, global C++ linkage)
BOOL SGetOpenFileNameA(LPOPENFILENAMEA p, DlgMode mode) { (void)p; (void)mode; return FALSE; }
BOOL SChooseColor(HWND parent, const COLORREF initClr[16], COLORREF *out)
{ (void)parent; (void)initClr; (void)out; return FALSE; }
