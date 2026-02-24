//#define _GNU_SOURCE
#include <stdlib.h>
//#include <stdio.h>
#include <string.h>

#include "stalloc.h"

/* static alloc */

typedef struct _stalloc_buf {
	void *buf;
	size_t size;
	size_t pos;
}
// __attribute__ ((__packed__))
stalloc_buf;

void *_calloc(size_t l){
	void *p;
#if _ALIGN && !(defined(MINIMAL) && _ALIGN < 4)
	if
#if _POSIX_C_SOURCE >= 200112L
	    (!posix_memalign(&p,_align(1),l)) memset(p,0,l);
#elif __STDC_VERSION__ >= 201112L || defined(_ISOC11_SOURCE)
	    ((p=aligned_alloc(_align(1),l))) memset(p,0,l);
#else
//#elif _XOPEN_SOURCE >= 500
	    ((p=valloc(l))) memset(p,0,l);
#endif
	else
#endif
	    {
		//fprintf(stderr,"ERROR posix_memalign %i\n",_align(1));
		p=calloc(1,l);
	}
	return p;
}

void *stalloc(size_t l){
	static const size_t st_block=1024*8;
	static stalloc_buf m = {};

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

