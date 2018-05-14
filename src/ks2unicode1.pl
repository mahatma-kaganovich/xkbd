#!/usr/bin/perl

$h='/usr/include/X11/keysymdef.h';

open($F,'<',$h)||die $!;
while(defined(my $s=<$F>)){
	(@a=$s=~/^\#define XK_([a-zA-Z_0-9]+)\s+0x([0-9a-f]+)\s*\/\* U+([0-9A-F]{4,6}) (.*) \*\/\s*$/) ||
	(@a=$s=~/^\#define XK_([a-zA-Z_0-9]+)\s+0x([0-9a-f]+)\s*\/\*\(U+([0-9A-F]{4,6}) (.*)\)\*\/\s*$/)||
	(@a=$s=~/^\#define XK_([a-zA-Z_0-9]+)\s+0x([0-9a-f]+)\s*(\/\*\s*(.*)\s*\*\/)?\s*$/)||next;

	(($u)=$a[2]=~/\s+U\+([A-Z0-9]{4})\s+/) || next;
	$k{hex($a[1])}=hex($u);
}
close($F);

$N=scalar(keys %k);

print "/* generated by $0 from $h */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>

#define ks2u_size $N
const struct ks2unicode {KeySym ks; unsigned short u;} ks2u[ks2u_size]={
";
for(sort{$a<=>$b}keys %k){
	print "{$_,$k{$_}},\n";
}

print "};\n";

print '
void ksText(KeySym ks, char **txt)
{
	char s[4];
	unsigned int n;
	unsigned int wc;
	struct ks2unicode *k2u;
	int p1 = 0;
	int p2 = ks2u_size-1;
	int p;
	KeySym k;

	if (!ks || *txt) return;
	if (ks > 0x01000000){
		wc = ks - 0x01000000;
		goto wide;
	}

	if (ks < ks2u[p1].ks || ks > ks2u[p2].ks) goto notfound;

next:
	p = (p1+p2)>>1;
	if (p == p1) goto notfound;
	k = ks2u[p].ks;
	if (ks > k) {
		p1 = p;
		goto next;
	}
	if (ks < k) {
		p2 = p;
		goto next;
	}
found:
	wc = ks2u[p].u;
wide:
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
		if (!*txt) sprintf(*txt=malloc(7),"U+%04X",wc);
		return;
	}
	s[n]=0;
	*txt = malloc(n);
	memcpy(*txt,s,n);
	(*txt)[n] = 0;
	return;
notfound:
	*txt=XKeysymToString(ks);
	if (!*txt) sprintf(*txt=malloc(10),"?%lx",ks);
	return;
}

';
