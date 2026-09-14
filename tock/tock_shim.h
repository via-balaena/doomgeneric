/* The contract between Doom's C and the Rust side of the port.
 *
 * Everything else in tock/ is ordinary C that could run anywhere. These four
 * are the only things that need a Tock process behind them, and libtock-rs
 * implements them. Keeping the list this short is the point: it is the whole
 * surface between the two languages, and it is checkable by `nm`.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* Write bytes to the process console. No newline translation, no buffering
 * promises -- the shim buffers, this just takes what it is given. */
void tock_console_write(const char *buf, size_t len);

/* Milliseconds since some fixed point. Only differences are used, so the
 * origin does not matter; it must not go backwards. */
uint32_t tock_ticks_ms(void);

/* Doom called exit() or I_Quit(). Does not return. */
void tock_exit(int status) __attribute__((noreturn));

/* The heap the shim's malloc hands out, supplied by the Rust side so the RAM
 * budget lives in one place. Doom's zone takes nearly all of it in one call. */
extern uint8_t *tock_heap_base;
extern size_t   tock_heap_size;
