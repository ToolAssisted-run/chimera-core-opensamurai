/* coro.h - a stack of its own for the game (coro.c) */
#ifndef SAMURAI_CORO_H
#define SAMURAI_CORO_H

#include <stddef.h>

typedef struct coro coro;
/* fn runs at the first resume; when it returns, the coroutine yields for ever */
coro *coro_create(void (*fn)(void), size_t stack_size);
void coro_resume(coro *c);   /* run c until it yields */
void coro_yield(coro *c);    /* inside c: back to the resumer */
void coro_destroy(coro *c);

#endif
