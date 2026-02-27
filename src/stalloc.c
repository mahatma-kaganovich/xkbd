/* static alloc */

#include <stdlib.h>
#include <string.h>
#include "stalloc.h"

#if __STDC_VERSION__ >= 201112L
#include <stddef.h>
#define CALIGN alignof(max_align_t)
#define _th thread_local
#else
#include <malloc.h>
#ifdef __BIGGEST_ALIGNMENT__
#define CALIGN __BIGGEST_ALIGNMENT__
#else
#define CALIGN 1
#endif
#define _th __thread
#endif

#ifndef _REENTRANT
#undef _th
#define _th
#endif

const size_t st_block=1024*8;

_th struct _stalloc_buf {
	void *buf;
	size_t size;
	size_t pos;
} m = {};

void *_calloc(size_t l){
	void *p;
#if _ALIGN
	if ((CALIGN&(BUF_ALIGN-1)) &&
#if __STDC_VERSION__ >= 201112L || defined(_ISOC11_SOURCE)
	    (p=aligned_alloc(BUF_ALIGN,l))
#elif defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE - 0) >= 200112L
	    !posix_memalign(&p,BUF_ALIGN,l)
#else
	    (p=valloc(l))
#endif
	) memset(p,0,l); else
#endif
		p=calloc(1,l);
	return p;
}

void *stalloc(size_t l){
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

