#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/random.h>
int randombytes(unsigned char *out,unsigned long long length) {
    while(length){size_t n=length>1048576?1048576:(size_t)length;ssize_t r=getrandom(out,n,0);
        if(r<0){if(errno==EINTR)continue;abort();}if(r==0)abort();out+=r;length-=(unsigned long long)r;}
    return 0;
}
