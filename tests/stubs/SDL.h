#include <stdint.h>
struct SDL_Thread;
uint32_t SDL_GetTicks();void SDL_Delay(uint32_t);
SDL_Thread* SDL_CreateThread(int (*)(void*),const char*,void*);
void SDL_WaitThread(SDL_Thread*,int*);
