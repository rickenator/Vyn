/* full_preproc.h - fixture for the libclang full-preprocessor bindgen backend
   (`vyb bindgen --full`). Exercises `#include` expansion + canonical-type
   resolution (<stdint.h> typedefs), conditional evaluation (#ifdef/#if), and
   object-like / constant-expression / function-like macros. */
#ifndef FULL_PREPROC_H
#define FULL_PREPROC_H

#include <stdint.h>

#ifdef USE_64
typedef int64_t word_t;
#else
typedef int32_t word_t;
#endif

#define COUNT 4
#define WIDE (2 * COUNT)
#define LIMIT_HI 0xFF00
#define SQUARE(x) ((x) * (x))
#define ADD2(a, b) ((a) + (b))

#if COUNT > 2
#define BIG_ENOUGH 1
#else
#define BIG_ENOUGH 0
#endif

typedef struct Vec2 {
    int32_t x;
    int32_t y;
} Vec2;

typedef enum Mode {
    MODE_A,
    MODE_B,
    MODE_C
} Mode;

int32_t compute(word_t base, int n);
uint64_t add64(uint64_t a, uint64_t b);
Mode classify(Vec2* p);
void scale(Vec2* out, int32_t f);

#endif
