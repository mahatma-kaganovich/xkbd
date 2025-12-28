#include <stdlib.h>

typedef struct _stalloc_buf {
	void *buf;
	int size;
	int pos;
}
 __attribute__ ((__packed__))
stalloc_buf;


void *stalloc(int l){
	static const int st_block=1024*8;
	static stalloc_buf m = {};

	if (m.size < l) goto new;
	m.buf+=m.pos;
a:
	m.size-=l;
	m.pos=l;
	return m.buf;
new:
	if (l >= st_block) return calloc(1,l);
	m.buf=calloc(1,m.size=st_block);
	goto a;
}
