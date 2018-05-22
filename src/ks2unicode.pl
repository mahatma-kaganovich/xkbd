#!/usr/bin/perl

$h='/usr/include/X11/keysymdef.h';
$packed='__attribute__ ((__packed__))';

open($F,'<',$h)||die $!;
while(defined(my $s=<$F>)){
	(@a=$s=~/^\#define XK_([a-zA-Z_0-9]+)\s+0x([0-9a-f]+)\s*\/\* U+([0-9A-F]{4,6}) (.*) \*\/\s*$/) ||
	(@a=$s=~/^\#define XK_([a-zA-Z_0-9]+)\s+0x([0-9a-f]+)\s*\/\*\(U+([0-9A-F]{4,6}) (.*)\)\*\/\s*$/)||
	(@a=$s=~/^\#define XK_([a-zA-Z_0-9]+)\s+0x([0-9a-f]+)\s*(\/\*\s*(.*)\s*\*\/)?\s*$/)||next;

	(($u)=$a[2]=~/\s+U\+([A-Z0-9]{4})\s+/) || next;
	$k{hex($i=$a[1])}=hex($u);
	
	$iu=~s/^0*//;
	$i=(length($i)+1)>>1;
	$ks_size=$i if($ks_size<$i)
}
close($F);

$KeySym = 'KeySym';
$ks_size *= 8;
$KeySym = "uint32_t" if($ks_size == 32);

open(STDOUT,'>','ks2unicode.c')||die $!;
print "/* generated by $0 from $h */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>

const struct ks2unicode {$KeySym ks; uint16_t u; uint8_t n;} $packed ks2u[]={
";

$x1=-1; $y1=-1; $n=-1;
$N=0;
for $x (sort{$a<=>$b}keys %k){
	$y=$k{$x};
	if($x!=$x1 || $y!=$y1 || $n==255) {
		print "$n},\n" if($n!=-1);
		print "{$x,$y,";
		$n=-1;
		$N++;
	}
	$x1=$x+1;
	$y1=$y+1;
	$n++;
}
print "$n}\n" if($n!=-1);
print "};

#define ks2u_size $N
";

print '

char ksText_buf[10];
static int n;

void ksText(KeySym ks, char **txt){
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
	k = ks2u[p].ks;
	if (ks > k) {
		if (p1 != p) {
			p1 = p;
			goto next;
		}
	} else if (ks < k) {
		if (p2 != p) {
			p2 = p;
			goto next;
		}
		/* happened??? */
		if (!p) goto notfound;
		k = ks2u[--p].ks;
	}
	n = ks - k;
	if (n < 0 || n > ks2u[p].n) goto notfound;
	wc = ks2u[p].u + n;
wide:
	if (wc < 0x000080) {
		n=1;
		ksText_buf[0]=(char)wc;
	} else if (wc < 0x000800) {
		n=2;
		ksText_buf[1] = (char) (0x80 | (wc & 0x3F)); wc >>= 6;
		ksText_buf[0] = (char) (0xC0 | wc);
	} else if (wc < 0x010000) {
		n=3;
		ksText_buf[2] = (char) (0x80 | (wc & 0x3F)); wc >>= 6;
		ksText_buf[1] = (char) (0x80 | (wc & 0x3F)); wc >>= 6;
		ksText_buf[0] = (char) (0xE0 | wc);
	} else if (wc < 0x200000) {
		n=4;
		ksText_buf[3] = (char) (0x80 | (wc & 0x3F)); wc >>= 6;
		ksText_buf[2] = (char) (0x80 | (wc & 0x3F)); wc >>= 6;
		ksText_buf[1] = (char) (0x80 | (wc & 0x3F)); wc >>= 6;
		ksText_buf[0] = (char) (0xF0 | wc);
	} else {
		*txt=XKeysymToString(ks);
		if (!*txt) n = sprintf(*txt = ksText_buf,"U+%04X",wc);
		return;
	}
	ksText_buf[n]=0;
	*txt = ksText_buf;
	return;
notfound:
	*txt=XKeysymToString(ks);
	if (!*txt) n = sprintf(*txt = ksText_buf,"?%lx",ks);
	return;
}

int ksText_(KeySym ks, char **txt){
	ksText(ks,txt);
	if (*txt == ksText_buf) {
		n++;
		memcpy(*txt = malloc(n), ksText_buf, n);
		return 1;
	}
	return 0;
}

';
