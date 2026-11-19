#include <stdio.h>

#undef putchar
#undef stdout
#undef stderr

extern int putchar(int ch);

FILE *const stdout = 0;
FILE *const stderr = 0;

int fputc(int ch, FILE *stream) {
  (void)stream;
  return putchar(ch);
}
