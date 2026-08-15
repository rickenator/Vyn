/* unions.h - fixture for C unions in the libclang full-preprocessor bindgen
   backend (`vyb bindgen --full`). A C union maps to a #[repr(C)] struct whose
   highest-alignment member is the accessible anchor plus a typed byte pad to
   the union's total size, so size and alignment match the C ABI. */
#ifndef UNIONS_H
#define UNIONS_H

typedef union Value {
    int i;
    float f;
    double d;
    void *p;
} Value;

typedef union Blob {
    char buf[16];
    double d;
} Blob;

typedef union { int i; double d; } Anon;

typedef struct Holder {
    Value value;
    long long tag;
} Holder;

int use_union(Value v, double *out);

#endif
