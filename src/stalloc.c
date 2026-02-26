#include <stdlib.h>
#include <string.h>
#include "stalloc.h"

#if __STDC_VERSION__ >= 201112L
#include <stddef.h>
#define CALIGN alignof(max_align_t)
#define _th thread_local
#else
#include <malloc.h>
#define CALIGN 1
#define _th __thread
#endif

#ifndef _REENTRANT
#undef _th
#define _th
#endif

/* static alloc */

typedef struct _stalloc_buf {
	void *buf;
	size_t size;
	size_t pos;
} stalloc_buf;

void *_calloc(size_t l){
	void *p;
#if _ALIGN
	if ((CALIGN&((1<<_ALIGN)-1)) &&
#if defined( _POSIX_C_SOURCE) && (_POSIX_C_SOURCE - 0) >= 200112L
	    !posix_memalign(&p,_align(1),l)
#elif __STDC_VERSION__ >= 201112L || defined(_ISOC11_SOURCE)
	    (p=aligned_alloc(_align(1),l))
#else
	    (p=valloc(l))
#endif
	) memset(p,0,l); else
#endif
		p=calloc(1,l);

	return p;
}

void *stalloc(size_t l){
	static const size_t st_block=1024*8;
	static _th stalloc_buf m = {};

	if (m.size < l) goto new;
	m.buf+=m.pos;
a:
	m.size-=l;
	m.pos=l;
	return m.buf;
new:
	if (l >= st_block) return _calloc(l);
	m.buf=_calloc(m.size=st_block);
	goto a;
}

