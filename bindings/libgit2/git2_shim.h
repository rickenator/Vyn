#pragma once
/* Curated libgit2 FFI surface - SDK Phase 3/4 (posted binding).
   Small subset of git2.h (avoids bindgen-ing the whole ~large header). Opaque
   libgit2 handles (git_repository / git_object / git_oid) are modeled as void*.
   Options param of git_clone is left void* so a caller may pass a null loc(0). */
typedef void git_repository;
typedef void git_object;
typedef void git_oid;

int git_libgit2_init(void);
int git_repository_open(git_repository** out, const char* path);
void git_repository_free(git_repository* repo);
int git_revparse_single(git_object** out, git_repository* repo, const char* revspec);
void git_object_free(git_object* obj);
const git_oid* git_object_id(git_object* obj);
int git_clone(git_repository** out, const char* url, const char* local_path, const void* options);
