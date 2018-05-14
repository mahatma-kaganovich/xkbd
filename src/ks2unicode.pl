#!/usr/bin/perl

$h='/usr/include/X11/keysymdef.h';

open($F,'<',$h)||die $!;
print "/* generated by $0 from $h */

#include <string.h>
#include <stdlib.h>
#include <X11/Xlib.h>

const struct ks2unicode {KeySym ks; unsigned short u;} ks2u[]={
";
while(defined(my $s=<$F>)){
	(@a=$s=~/^\#define XK_([a-zA-Z_0-9]+)\s+0x([0-9a-f]+)\s*\/\* U+([0-9A-F]{4,6}) (.*) \*\/\s*$/) ||
	(@a=$s=~/^\#define XK_([a-zA-Z_0-9]+)\s+0x([0-9a-f]+)\s*\/\*\(U+([0-9A-F]{4,6}) (.*)\)\*\/\s*$/)||
	(@a=$s=~/^\#define XK_([a-zA-Z_0-9]+)\s+0x([0-9a-f]+)\s*(\/\*\s*(.*)\s*\*\/)?\s*$/)||next;

	(($u)=$a[2]=~/\s+U\+([A-Z0-9]{4})\s+/) || next;
	print "{0x$a[1],0x$u},\n";
}
print "{0,0}\n};\n";

print '
void ksText(KeySym ks, char **txt)
{
	char s[4];
	unsigned int n;
	unsigned int wc;
	struct ks2unicode *k2u;

	if (!ks || *txt) return;
	if (ks > 0x01000000){
		wc = ks - 0x01000000;
	} else {
		for(k2u = (struct ks2unicode*)&ks2u; k2u->ks && k2u->ks!=ks; k2u+=sizeof(*k2u)){};
		if(!(wc=k2u->u)) {
			*txt=XKeysymToString(ks);
			return;
		}
	}
	if (wc < 0x000080) {
		n=1;
		s[0]=(char)wc;
	} else if (wc < 0x000800) {
		n=2;
		s[1] = (char) (0x80 | (wc & 0x3F)); wc >>= 6;
		s[0] = (char) (0xC0 | wc);
	} else if (wc < 0x010000) {
		n=3;
		s[2] = (char) (0x80 | (wc & 0x3F)); wc >>= 6;
		s[1] = (char) (0x80 | (wc & 0x3F)); wc >>= 6;
		s[0] = (char) (0xE0 | wc);
	} else if (wc < 0x200000) {
		n=4;
		s[3] = (char) (0x80 | (wc & 0x3F)); wc >>= 6;
		s[2] = (char) (0x80 | (wc & 0x3F)); wc >>= 6;
		s[1] = (char) (0x80 | (wc & 0x3F)); wc >>= 6;
		s[0] = (char) (0xF0 | wc);
	} else {
		*txt=XKeysymToString(ks);
		return;
	}
	s[n]=0;
	*txt = malloc(n);
	memcpy(*txt,s,n);
	(*txt)[n] = 0;
	return;
}

';
