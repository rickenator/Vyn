// SPDX-License-Identifier: Apache-2.0

/* libsample.h - minimal C header fixture for `vyb bindgen`.
   The declarations here are a subset of what libc provides so the generated
   bindings resolve against the host process at JIT/link time. */
#ifndef LIBSAMPLE_H
#define LIBSAMPLE_H

#include <stddef.h>

typedef unsigned long size_t;

/* A small color enum */
typedef enum Color {
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE
} Color;

/* A bitmask mode enum with explicit values */
typedef enum Mode {
    MODE_EXACT = 1,
    MODE_FAST = 2,
    MODE_PREFETCH = 4
} Mode;

/* Max item count constant */
#define MAX_ITEMS 16

/* A 2D point */
typedef struct Point {
    int x;
    int y;
} Point;

/* libc-style functions the generated module can call */
extern int puts(const char *s);
extern size_t strlen(const char *s);

#endif /* LIBSAMPLE_H */
