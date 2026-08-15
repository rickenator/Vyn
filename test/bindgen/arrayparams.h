/* arrayparams.h - fixture exercising C array-parameter decay for `vyb bindgen`.
   In C, a function parameter written as `T a[N]` / `T a[]` decays to `T*`,
   so `char s[]` is a `char*` (CString), not a scalar byte. These declarations
   mirror real libc symbols so the generated `share(all)` bindings resolve
   against the host process at JIT/link time. */
#ifndef ARRAYPARAMS_H
#define ARRAYPARAMS_H

#include <stddef.h>

extern size_t strlen(const char s[]);
extern char *strcpy(char dest[], const char src[]);
extern int strcmp(const char a[], const char b[]);
extern double sumvals(const double vals[], size_t n);

#endif /* ARRAYPARAMS_H */
