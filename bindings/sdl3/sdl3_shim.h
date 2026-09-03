#pragma once
/* Curated SDL3 FFI surface - posted Vyb binding (#174).
   Signatures mirror SDL3 3.4.x, verified against the bundled SDL3 headers:
   note SDL_CreateRenderer(window, name) has NO flags argument in SDL3, and
   SDL_Init / SDL_SetRenderDrawColor / SDL_RenderClear / SDL_RenderPresent
   return bool (1 = success). ABI: those bools are returned in AL and read as
   0/1, so they are declared `int` for the trivial Vyb FFI types.
   Opaque SDL_Window* / SDL_Renderer* handles are modeled as void* (ABI
   identical; keeps the Vyb FFI types trivial), like the sqlite/libgit2 shims.

   Flag values (SDL3 headers) are kept out of preprocessor range on purpose
   so the Vyb wrapper hardcodes them as plain literals:
     SDL_INIT_VIDEO    = 0x20u
     SDL_WINDOW_HIDDEN = 0x08u                                         */

typedef void SDL_Window;
typedef void SDL_Renderer;

int  SDL_Init(unsigned int flags);          /* returns bool: 1 = success      */
void SDL_Quit(void);

SDL_Window*   SDL_CreateWindow(const char* title, int w, int h, unsigned int flags);
void          SDL_DestroyWindow(SDL_Window* window);

SDL_Renderer* SDL_CreateRenderer(SDL_Window* window, const char* name);
void          SDL_DestroyRenderer(SDL_Renderer* renderer);

int SDL_SetRenderDrawColor(SDL_Renderer* renderer, unsigned char r,
                           unsigned char g, unsigned char b, unsigned char a); /* bool */
int SDL_RenderClear(SDL_Renderer* renderer);                                  /* bool */
int SDL_RenderPresent(SDL_Renderer* renderer);                                /* bool */

void        SDL_Delay(unsigned int ms);
const char* SDL_GetError(void);
