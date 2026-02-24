
#ifndef MINIMAL
// 128-bit alignment if possible
#define STALLOC 4
#else
// or 3
#define STALLOC 0
#endif

void *stalloc(size_t l);

#ifndef STALLOC
#define _ALIGN 0
#define calloc1(s) calloc(1,sizeof(s))
#define malloc1(s) malloc(sizeof(s))
#define malloc2(s) malloc(s)
#else

// test over _GNU_SOURCE. req. at least vmalloc
//#if _POSIX_C_SOURCE >= 200112L || __STDC_VERSION__ >= 201112L || defined(_ISOC11_SOURCE) || _XOPEN_SOURCE >= 500
#define _ALIGN STALLOC
//#else
// 64-bit malloc
//#define _ALIGN 3
//#endif

#define _align(s) (((((s)+((1<<_ALIGN)-1)))>>_ALIGN)<<_ALIGN)
#define calloc1(s) stalloc(_align(sizeof(s)))
#define malloc1(s) stalloc(_align(sizeof(s)))
#define malloc2(s) stalloc(_align(s))
#endif



