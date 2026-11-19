#ifndef KEYGEN_NATIVE_COMPAT_H
#define KEYGEN_NATIVE_COMPAT_H
#include <stddef.h>
#include <string.h>
static inline void keygen_native_bzero(void *ptr, size_t size)
{
    static void *(*const volatile erase)(void *, int, size_t) = memset;
    erase(ptr, 0, size);
}
#define explicit_bzero keygen_native_bzero
#endif
