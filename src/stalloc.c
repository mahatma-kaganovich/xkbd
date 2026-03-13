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
#define _th thread_local
#if __STDC_VERSION__ < 202311L
#endif
#define CALIGN alignof(max_align_t)
#else
#include <malloc.h>
#define _th __thread
#ifdef __BIGGEST_ALIGNMENT__
#define CALIGN __BIGGEST_ALIGNMENT__
#else
#define CALIGN 1
#endif
#endif //  __STDC_VERSION__ >= 201112L

#ifndef _REENTRANT
#undef _th
#define _th
#endif

#define  buf_align (1<<BUF_ALIGN)

const size_t st_block=1024*8;

_th struct _stalloc_buf {
	void  *buf;
	size_t size;
	size_t pos;
} m = {};

static inline void *_alloc(size_t l){
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
	) return p;
#endif
	return NULL;
}

static void *_calloc(size_t l){
	void *p;
#if _ALIGN
	if ((p=_alloc(buf_align))) memset(p,0,l); else
#endif
		p=calloc(1,l);
	return p;
}

static void *_malloc(size_t l){
	void *p;
#if _ALIGN
	if (!(p=_alloc(buf_align)))
#endif
		p=malloc(l);
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
#if 0
	m.size = 0xffffffff;
#else
	// >=st_block: (ceil(st_block/pgs)*pgs)
	// ++: ((abs(st_block/pgs)+1)*pgs)
	m.size = sysconf(_SC_PAGESIZE);
	m.size *= st_block/m.size + 1;
#endif
	m.buf = mmap(NULL, m.size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (m.buf == MAP_FAILED)
#endif
	    m.buf=_calloc(m.size=st_block);
	goto a;
}

void *ststrdup(const char *s){
	void *d;
#if __STDC_VERSION__ >= 202311L || defined(_POSIX_C_SOURCE)
	// may be slower, but give him chance
	m.buf+=m.pos;
	if ((d = memccpy(m.buf,s,0,m.size))) {
		m.pos = d - m.buf;
		return m.buf;
	}
	m.pos=0;
	memset(m.buf,0,m.size);
#endif
	int l = strlen(s)+1;
	d = (l > (st_block>>1)) ? _malloc(l) : stalloc(_align(l));
	memcpy(d,s,l);
	return d;
}

void stline(){
	if (BUF_ALIGN > _ALIGN) stalloc(buf_align - (m.size&(buf_align-1)));
}

