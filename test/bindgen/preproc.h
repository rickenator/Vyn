// SPDX-License-Identifier: Apache-2.0

/* preproc.h - fixture exercising `#define` object-like macros for `vyb bindgen`.
   Numeric/string `#define`s become shared constant functions; function-like and
   multi-line macros are skipped with a warning (a full preprocessor is libclang
   territory). */
#ifndef PREPROC_H
#define PREPROC_H

#define MAX_BUFSIZE 4096
#define DEFAULT_NAME "vyb"
#define SCALE_FACTOR 2.5
#define FLAG_ENABLED 1
#define MASK 0xff00
#define TWEAK -2

#define WRAPPED(x) ((x) * 2)

#endif /* PREPROC_H */
