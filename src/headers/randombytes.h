#ifndef __RANDOMBYTES_H
#define __RANDOMBYTES_H

#ifdef WIN32
#include <wincrypt.h>
#endif

void randombytes(void *ptr, size_t length);
void srandom_init(void);

#endif

