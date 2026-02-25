
#ifndef MINIMAL
// 128-bit alignment if possible
#define STALLOC 4
#else
#define STALLOC 0
#endif

void *stalloc(size_t l);

#ifndef STALLOC
#define _ALIGN 0
#define calloc1(s) calloc(1,sizeof(s))
#define malloc1(s) malloc(sizeof(s))
#define malloc2(s) malloc(s)
#else
#define _ALIGN STALLOC
#define _align(s) (((((s)+((1<<_ALIGN)-1)))>>_ALIGN)<<_ALIGN)
#define calloc1(s) stalloc(_align(sizeof(s)))
#define malloc1(s) stalloc(_align(sizeof(s)))
#define malloc2(s) stalloc(_align(s))
#endif



