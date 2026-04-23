/* static alloc / (c) mahatma / GPLv2 or Anarchy license */

//#define USE_MMAP

#ifdef ENABLE_HUGE_MMAP
#define USE_MMAP
#endif

#include <stdlib.h>
#include <string.h>
#include "stalloc.h"

#ifdef _POSIX_C_SOURCE
#include <unistd.h>
#if defined(USE_MMAP) && defined(_POSIX_MAPPED_FILES) && (_POSIX_MAPPED_FILES - 0) > 0
#include <sys/mman.h>
#endif
#endif

#if __STDC_VERSION__ >= 201112L
#include <stddef.h>
#if __STDC_VERSION__ < 202311L
#endif
#define CALIGN alignof(max_align_t)
#else
#include <malloc.h>
#ifdef __BIGGEST_ALIGNMENT__
#define CALIGN __BIGGEST_ALIGNMENT__
#else
#define CALIGN 1
#endif
#endif //  __STDC_VERSION__ >= 201112L

#if defined(_POSIX_MAPPED_FILES) && (_POSIX_MAPPED_FILES - 0) > 0
#else
#undef USE_MMAP
#undef ENABLE_HUGE_MMAP
#endif

#define  buf_align (1<<BUF_ALIGN)

#ifdef ENABLE_HUGE_MMAP
const size_t st_block=0xffffffff;
#else
const size_t st_block=4096*3;
#endif

_th stalloc_buf_t m = {};

static void *O0M(){
//	return NULL;
	fprintf(stderr,"Out of memory\n");
	abort();
}

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
#ifdef ENABLE_HUGE_MMAP
	if (m.buf == MAP_FAILED) O0M();
#endif
}
#endif

void stalloc3_init(){
next:
	stalloc(_align(1));
	m.size -= m.pos;
	m.pos = 0;
#ifdef ENABLE_HUGE_MMAP
	if (allocs == MAP_FAILED) {
		m.size = 0;
		allocs = m.buf;
		goto next;
	}
#endif
}

#ifndef ENABLE_HUGE_MMAP
static void __aligned *_alloc(size_t l){
	void __aligned *p;
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

//static void __aligned *_calloc(size_t l){
static void __aligned *_calloc(size_t l){
	void __aligned *p;
#if _ALIGN
	if ((p=_alloc(l))) memset(p,0,l); else
#endif
		p=calloc(1,l);
	return p?:O0M();
}

static void __aligned *_malloc(size_t l){
	void __aligned *p;
#if _ALIGN
	if (!(p=_alloc(l)))
#endif
		p=malloc(l);
	return p?:O0M();
}
#endif

void __aligned *stalloc(size_t l){
#if defined(__GNUC__) || defined(__clang__)
	if (__builtin_expect(m.size < l,0)) goto new;
#else
	if (m.size < l) goto new;
#endif
	m.buf+=m.pos;
a:
	m.size-=(m.pos=l);
	return m.buf;
new:
	if (l > st_block)
#ifdef ENABLE_HUGE_MMAP
	    return O0M();
#else
	    return _calloc(l);
#endif
#ifdef USE_MMAP
	_mmap();
#ifndef ENABLE_HUGE_MMAP
	if (m.buf == MAP_FAILED)
#endif
#endif
#ifndef ENABLE_HUGE_MMAP
	    m.buf=_calloc(m.size=st_block);
#endif
	goto a;
}

void __aligned *ststrdup(const char *s){
	void __aligned *d;
#ifdef ENABLE_HUGE_MMAP
	m.buf+=m.pos;
rep:
	if ((d = memccpy(m.buf,s,0,m.size))) {
		m.pos = _align(d - m.buf);
		m.size -= m.pos;
		return m.buf;
	}
	m.pos = 0;
	if (m.size > (st_block>>1)) return O0M();
	_mmap();
	goto rep;
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

void __aligned *ststrdup_buf(const char *s,size_t n){
	if (n > m.size) return ststrdup(s);
	m.buf+=m.pos;
	m.pos = _align((void*)stpcpy(m.buf,s) - m.buf + 1);
	m.size -= m.pos;
	return m.buf;
}

#ifdef ENABLE_HUGE_MMAP
_th void *allocs = MAP_FAILED;
#else
_th void *allocs[] = {};
#endif

void stline(){
	if (BUF_ALIGN > _ALIGN) stalloc(buf_align - (m.size&(buf_align-1)));
}

