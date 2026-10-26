/* glib's static vasnprintf (gnulib) converts wide format args by calling
 * wcrtomb through an import pointer: call *__imp_wcrtomb(%rip). Older
 * mingw-w64 runtimes lack an __imp_wcrtomb data symbol. The previous -Wl,--defsym alias
 * pointed that pointer at the wcrtomb FUNCTION ENTRY, so every indirect
 * call executed machine code as an address and died with 0xc0000005 at the
 * call site (seen as explorer.exe / gotv.exe crash loops). A real data slot
 * holding the address of wcrtomb satisfies the reference correctly.
 * Newer mingw-w64 CRT versions (GCC 16+) now provide __imp_wcrtomb in libmsvcrt.a,
 * so we mark it __attribute__((selectany)) to avoid duplicate symbol conflicts.
 * Only <stddef.h> here (for wchar_t): <wchar.h>/<stdio.h> declare wcrtomb
 * dllimport on mingw, which would re-import __imp_wcrtomb from msvcrt. */
#include <stddef.h>

typedef int (*gtv_wcrtomb_fn)(char *, const wchar_t *);

int wcrtomb(char *s, const wchar_t *wchar_ptr);

__attribute__((selectany)) gtv_wcrtomb_fn const __imp_wcrtomb = wcrtomb;
