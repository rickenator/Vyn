/* fnpointer.h - fixture exercising C function-pointer parameters for `vyb bindgen`.
   Inline (`void (*cb)(...)`) and typedef'd (`op_fn`) C function pointers bind as
   Vyb `fn(...) -> ...` types. Declarations are self-contained; the symbols are
   never called, so the generated bindings only need to import and compile. */
#ifndef FNPARAMS_H
#define FNPARAMS_H

#include <stddef.h>

typedef int (*op_fn)(int a, int b);

int run_cb(void (*cb)(int, void *ctx), int arg);
double apply_cb(double (*f)(double, double), double a, double b);
int use_op(op_fn f, int a, int b);
char *map_str(char *(*cb)(int));
void on_quit(void (*quit)(void));

#endif /* FNPARAMS_H */
