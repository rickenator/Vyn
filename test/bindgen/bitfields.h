/* bitfields.h - fixture exercising C bitfields for `vyb bindgen`. A bitfield
   packs bits into shared storage and cannot be ABI-represented as a Vyb struct
   field, so a struct containing bitfields is skipped with a warning; plain
   structs beside them still bind. */
#ifndef BITFIELDS_H
#define BITFIELDS_H

typedef struct Flags {
    unsigned int is_on : 1;
    unsigned int mode : 3;
    int value;
} Flags;

typedef struct PlainHandle {
    int fd;
} PlainHandle;

#endif /* BITFIELDS_H */
