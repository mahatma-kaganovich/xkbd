/* static alloc */

#include <stdlib.h>
#include <string.h>
#include "stalloc.h"

#ifdef _POSIX_C_SOURCE
#include <unistd.h>
//#undef _POSIX_MAPPED_FILES
#if defined(_POSIX_MAPPED_FILES) && (_POSIX_MAPPED_FILES - 0) > 0
#include <sys/mman.h>
#endif
#endif

#if __STDC_VERSION__ >= 201112L
#include <stddef.h>
#if __STDC_VERSION__ < 202311L
#include <stdalign.h>
#endif
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

#define  buf_align (1<<BUF_ALIGN)

const size_t st_block=1024*8;

_th struct _stalloc_buf {
	void *buf;
	size_t size;
	size_t pos;
} m = {};

void *_calloc(size_t l){
	void *p;
#if _ALIGN
	if ((CALIGN&(buf_align-1)) &&
#if __STDC_VERSION__ >= 201112L || defined(_ISOC11_SOURCE)
	    (p=aligned_alloc(buf_align,l))
#elif defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE - 0) >= 200112L
	    !posix_memalign(&p,buf_align,l)
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
	m.size-=(m.pos=l);
	return m.buf;
new:
	if (l >= st_block) return _calloc(l);
#if defined(_POSIX_MAPPED_FILES) && (_POSIX_MAPPED_FILES - 0) > 0
	m.size = sysconf(_SC_PAGESIZE);
	m.buf = mmap(NULL, m.size += st_block/m.size*m.size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (m.buf == MAP_FAILED)
#endif
	    m.buf=_calloc(m.size=st_block);
	goto a;
}

void *ststrdup(const char *s){
	int l = strlen(s)+1;
	void *d = (l > (st_block>>1)) ? malloc(l) : stalloc(_align(l));
	memcpy(d,s,l);
	return d;
}

void stline(){
	if (BUF_ALIGN > _ALIGN) stalloc(buf_align - (m.size&(buf_align-1)));
}

