/* arrstruct.h - fixture for fixed-size C array struct fields in the libclang
   full-preprocessor bindgen backend (`vyb bindgen --full`). Fixed-size array
   members bind as contiguous Vyb value-array fields (`[Elem; N]`), including
   nested and typedef'd arrays; a flexible array member (`int data[]`) cannot
   be ABI-represented and skips its struct with a warning. */
#ifndef ARRSTRUCT_H
#define ARRSTRUCT_H

typedef char name_t[8];

typedef struct Record {
    char name[8];
    int values[4];
    double matrix[2][3];
    name_t alias;
} Record;

typedef struct Buffer {
    long long data[4];
    int len;
} Buffer;

typedef struct Flex {
    int size;
    int data[];
} Flex;

#endif
