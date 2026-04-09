/* static alloc / (c) mahatma / GPLv2 or Anarchy license */


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

#if defined(_POSIX_MAPPED_FILES) && (_POSIX_MAPPED_FILES - 0) > 0
#define USE_MMAP
#else
#undef USE_MMAP
#undef ENABLE_HUGE_MMAP
#endif


#define  buf_align (1<<BUF_ALIGN)

#ifdef ENABLE_HUGE_MMAP
const size_t st_block=0xffffffff;
#else
const size_t st_block=1024*8;
#endif

_th struct _stalloc_buf {
	void  *buf;
	size_t size;
	size_t pos;
} m = {};

#ifdef USE_MMAP
#ifdef ENABLE_HUGE_MMAP
__attribute__((noinline))
#endif
static void _mmap(){
#ifdef ENABLE_HUGE_MMAP
	m.size = st_block;
#else
	// >=st_block: (ceil(st_block/pgs)*pgs)
	// ++: ((abs(st_block/pgs)+1)*pgs)
	m.size = sysconf(_SC_PAGESIZE);
	m.size *= st_block/m.size + 1;
#endif
	m.buf = mmap(NULL, m.size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS
#if defined(ENABLE_HUGE_MMAP) && (ENABLE_HUGE_MMAP+0) != 2
		| MAP_NORESERVE
#endif
		, -1, 0);
}
#endif


#ifndef ENABLE_HUGE_MMAP
static void *_alloc(size_t l){
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
#endif

void *stalloc(size_t l){
	if (m.size < l) goto new;
	m.buf+=m.pos;
a:
	m.size-=(m.pos=l);
	return m.buf;
new:
	if (l > st_block)
#ifdef ENABLE_HUGE_MMAP
	    return NULL;
#else
	    return _calloc(l);
#endif
#ifdef USE_MMAP
	_mmap();
	if (m.buf == MAP_FAILED)
#endif
#ifdef ENABLE_HUGE_MMAP
	    return NULL;
#else
	    m.buf=_calloc(m.size=st_block);
#endif
	goto a;
}

void *ststrdup(const char *s){
	void *d;
#ifdef ENABLE_HUGE_MMAP
	m.buf+=m.pos;
rep:
	if ((d = memccpy(m.buf,s,0,m.size))) {
		m.pos = _align(d - m.buf);
		m.size -= m.pos;
		return m.buf;
	}
	m.pos = 0;
	if (m.size < (st_block>>1)) {
		_mmap();
		if (m.buf != MAP_FAILED) goto rep;
	}
	return NULL;
#else
#if !defined(MINIMAL) && \
    (__STDC_VERSION__ >= 202311L || defined(_POSIX_C_SOURCE)) && \
    !(__GLIBC__ && !__s390x__)
	// may be slower, but give him chance
	// keep "right" code for 1-pass memccpy()
	m.buf+=m.pos;
	if ((d = memccpy(m.buf,s,0,m.size))) {
		m.pos = _align(d - m.buf);
		m.size -= m.pos;
		return m.buf;
	}
	size_t l = strlen(s);
	if (l >= (st_block>>1)) {
		m.pos=0;
		memset(m.buf,0,m.size);
		d = _malloc(++l);
	} else
		d = stalloc(_align(l+1));
#else
	size_t l = strlen(s);
	d = (l >= (st_block>>1)) ? _malloc(++l) : stalloc(_align(l+1));
#endif
	memcpy(d,s,l);
	return d;
#endif
}

void *ststrdup_buf(const char *s,size_t n){
	if (n > m.size) return ststrdup(s);
	m.buf+=m.pos;
	m.pos = _align((void*)stpcpy(m.buf,s) - m.buf + 1);
	m.size -= m.pos;
	return m.buf;
}

void stline(){
	if (BUF_ALIGN > _ALIGN) stalloc(buf_align - (m.size&(buf_align-1)));
}

