/* static alloc / (c) mahatma / GPLv2 or Anarchy license */

#ifdef MINIMAL
#define ENABLE_HUGE_MMAP
#define STALLOC 0
#endif

#ifndef STALLOC
// 128-bit alignment if possible
#define STALLOC 4
#endif

// 6 to 64 = usual cache line
// _ALIGN to code optimize
#define BUF_ALIGN _ALIGN

void *stalloc(size_t l);
void *ststrdup(const char *s);
void *ststrdup_buf(const char *s,size_t n);

#define strdup2(d,s,size) memcpy(d = malloc2(\
	(sizeof(s) > sizeof(void*) && sizeof(s) <= _align(1))? sizeof(s) : size+1\
    ),s,_ALIGN ? size : (size+1))

#if STALLOC == -1
#define _ALIGN 0
#define _align(s) (s)
#define calloc1(s) calloc(1,sizeof(s))
#define malloc1(s) malloc(sizeof(s))
#define malloc2(s) malloc(s)
#define strdup1(s) strdup(s)
#define free1(s) free(s)
#else
#define _ALIGN STALLOC
//#define _align(s) (((((s)+((1<<_ALIGN)-1)))>>_ALIGN)<<_ALIGN)
#define _align(s) ((((size_t)(s)+(((size_t)1<<_ALIGN)-1)))&~(((size_t)1<<_ALIGN)-1))
#define calloc1(s) stalloc(_align(sizeof(s)))
#define malloc1(s) stalloc(_align(sizeof(s)))
#define malloc2(s) stalloc(_align(s))
#define strdup1(s) (\
	(sizeof(s) > sizeof(void*))?ststrdup_buf(s,_align(sizeof(s))):\
	ststrdup(s)\
	)
#define free1(s) {}
#endif
