/* static alloc / (c) mahatma / GPLv2 or Anarchy license */

#ifndef _STALLOC_H_
#define _STALLOC_H_

#include <stdio.h>

#ifndef __SIZEOF_POINTER__
#include <stdint.h>
#if UINTPTR_MAX == 0xfffff
#define __SIZEOF_POINTER__ 2
#elif UINTPTR_MAX == 0xffffffff
#define __SIZEOF_POINTER__ 4
#elif UINTPTR_MAX == 0xffffffffffffffff
#define __SIZEOF_POINTER__ 8
#elif UINTPTR_MAX == 0xffffffffffffffffffffffffffffffff
#define __SIZEOF_POINTER__ 16
#endif
#endif

#ifdef MINIMAL
#define ENABLE_HUGE_MMAP
#define STALLOC 0
#endif

#ifndef STALLOC
// 128-bit alignment if possible
#define STALLOC 4
#endif

#ifndef _REENTRANT
#define _th
#elif __STDC_VERSION__ >= 201112L
#define _th thread_local
#else
#define _th __thread
#endif

// 6 to 64 = usual cache line
// _ALIGN to code optimize
#define BUF_ALIGN _ALIGN


#define MAX_ALLOC_FREE 1024

#define strdup2(d,s,size) memcpy(d = malloc2(\
	(sizeof(s) > sizeof(void*) && sizeof(s) <= _align(1))? sizeof(s) : size+1\
    ),s,_ALIGN ? size : (size+1))

#define _ALIGN_(s,a) (((((s)+((1<<(a))-1)))>>(a))<<(a))
#define _align_(s,a) ((((size_t)(s)+(((size_t)1<<(a))-1)))&~(((size_t)1<<(a))-1))

#if __SIZEOF_POINTER__ == 2
#define _PTR_PW 1
#elif __SIZEOF_POINTER__ == 4
#define _PTR_PW 2
#elif __SIZEOF_POINTER__ == 8
#define _PTR_PW 3
#elif __SIZEOF_POINTER__ == 16
#define _PTR_PW 4
#else
#undef _PTR_PW
#error macro pointer size unknown
#endif

#if STALLOC == -2
#define _ALIGN _PTR_PW
#endif

#define __aligned

#if STALLOC == -1
#define _ALIGN 0
#define _align(s) (s)
#define calloc1(s) calloc(1,sizeof(s))
#define malloc1(s) malloc(sizeof(s))
#define malloc2(s) malloc(s)
#define malloc3(s) malloc(s)
#define strdup1(s) strdup(s)
#define free1(s) free(s)
#define free3(s) free(s)
#else
#define _ALIGN STALLOC
#define _align(s) _align_(s,_ALIGN)
#define calloc1(s) malloc2(sizeof(s))
#define malloc1(s) malloc2(sizeof(s))
#define malloc2(s) stalloc(_align(s))
#define malloc3(s) stalloc3(sizeof(s))
#define strdup1(s) (\
	(sizeof(s) > sizeof(void*))?ststrdup_buf(s,_align(sizeof(s))):\
	ststrdup(s)\
	)
#define free1(s) {}
#define free3(s) stfree3(s,sizeof(*s))
#if defined(__GNUC__) || defined(__clang__)
#undef __aligned
#define __aligned __attribute__((aligned(_align_(1,_ALIGN))))
#endif
#endif


#undef _PRE_ALIGN
#undef _POST_ALIGN

#ifndef _PTR_PW
#define _PRE_ALIGN(x) (((((x)+((1<<_ALIGN)-1)))>>_ALIGN)*sizeof(void*))
#elif _PTR_PW == _ALIGN
#define _PRE_ALIGN(x) _align(x)
#define _POST_ALIGN(l,a) (a)
#elif _PTR_PW < _ALIGN
#define _POST_ALIGN(l,a) ((a)<<(_ALIGN-_PTR_PW))
#else
#define _POST_ALIGN(l,a) ((a)>>(_PTR_PW-_ALIGN))
#endif

#ifndef _PRE_ALIGN
#define _PRE_ALIGN(x) (((((x)+((1<<_ALIGN)-1)))>>_ALIGN)<<_PTR_PW)
#endif
#ifndef _POST_ALIGN
//#define _POST_ALIGN(l,a) (((a)>>_PTR_PW)<<_ALIGN)
#define _POST_ALIGN(l,a) _align(l)
#endif

void __aligned *stalloc(size_t l);
void __aligned *ststrdup(const char *s);
void __aligned *ststrdup_buf(const char *s,size_t n);


extern _th void *allocs[_ALIGN_(MAX_ALLOC_FREE+1,_ALIGN)];

// sized malloc/free, const optimized
// minimal code - align == pointer / -DSTALLOC=-2
static inline void __aligned *stalloc3(size_t l){
	size_t a = _PRE_ALIGN(l);
	if (a < _PRE_ALIGN(sizeof(void*)) || a > _PRE_ALIGN(MAX_ALLOC_FREE)) {
		fprintf(stderr,"stalloc3 - bad alloc %lu\n",l);
		return malloc(l);
	}
	void **ap = ((void*)&allocs)+a;
	void **p = *ap;
	if (!p) return stalloc(_POST_ALIGN(l,a));
	*ap = *p;
	return p;
}

static inline void stfree3(void *p,size_t l){
	size_t a = _PRE_ALIGN(l);
	if (a < _PRE_ALIGN(sizeof(void*)) || a > _PRE_ALIGN(MAX_ALLOC_FREE)) {
		free(p);
		return;
	}
	void **ap = ((void*)&allocs)+a;
	*(void **)p = *ap;
	*ap = p;
}

#endif
