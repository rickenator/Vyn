#pragma once
/* Curated SQLite3 FFI surface - SDK Phase 3 (#174). Small, declarable subset
   of sqlite3.h (avoids bindgen-ing the full ~100KB header). The opaque
   `sqlite3*` handle is modeled as `void*` (ABI-identical; keeps the Vyb FFI
   types trivial). */
typedef void (*sqlite3_callback)(void*, int, char**, char**);
int sqlite3_open(const char* filename, void** ppDb);
int sqlite3_close(void* db);
int sqlite3_exec(void* db, const char* sql, sqlite3_callback cb, void* arg, char** errmsg);
const char* sqlite3_errmsg(void* db);
