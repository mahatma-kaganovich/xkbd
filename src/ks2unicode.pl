#!/usr/bin/perl

%tr=(
'KP_Decimal'=>'decimalpoint',
'KP_Add'=>'plus',
'KP_Subtract'=>'minus',
'KP_Divide'=>'division',

'Escape'=>'Esc',
'BackSpace'=>'BkSp',
'Caps_Lock'=>'Caps',
'Return'=>'Enter',
'Shift_L'=>'Shift',
'Shift_R'=>'Shift',
'Control_L'=>'Ctrl',
'Control_R'=>'Ctrl',
'Super_L'=>'Super',
'Super_R'=>'Super',
'Hyper_L'=>'Hyper',
'Hyper_R'=>'Hyper',
'Alt_L'=>'Alt',
'Alt_R'=>'Alt',
'Up'=>'↑',
'Left'=>'←',
'Down'=>'↓',
'Right'=>'→',
'ISO_Next_Group'=>'grp',
'Page_Up'=>'Pg↑',
'Page_Down'=>'Pg↓',
'Scroll_Lock'=>'SclLk',
'Num_Lock'=>'NumLk',
'Sys_Req'=>'SysRq',
'Meta_R'=>'Meta',
'Meta_L'=>'Meta',
);

$h='/usr/include/X11/keysymdef.h';
$packed='__attribute__ ((__packed__))';

open($F,'<',$h)||die $!;
while(defined(my $s=<$F>)){
	(@a=$s=~/^\#define XK_([a-zA-Z_0-9]+)\s+0x([0-9a-f]+)\s*\/\* U+([0-9A-F]{4,6}) (.*) \*\/\s*$/) ||
	(@a=$s=~/^\#define XK_([a-zA-Z_0-9]+)\s+0x([0-9a-f]+)\s*\/\*\(U+([0-9A-F]{4,6}) (.*)\)\*\/\s*$/)||
	(@a=$s=~/^\#define XK_([a-zA-Z_0-9]+)\s+0x([0-9a-f]+)\s*(\/\*\s*(.*)\s*\*\/)?\s*$/)||next;

	$d=hex($i=$a[1]);
	$kc{$a[0]}=$d;
	(($u)=$a[2]=~/[ \(]U\+([A-Z0-9]{4})\s+/)||next;
	$u=hex($u);
	$k{$d}=$u;
	$iu=~s/^0*//;
	$i=(length($i)+1)>>1;
	$ks_size=$i if($ks_size<$i)
}
close($F);

sub ok{
	for(@_) {
		if(exists($kc{$_}) && exists($k{$kc{$_}})) {
			$k{$kc}=$k{$kc{$_}};
			return 1;
		}
	}
}

for(sort keys %kc){
	exists($k{$kc{$_}}) && next;
	$i=$_;
	$i0='';
	$kc=$kc{$_};

	while($i0 ne $i) {
		$i0=$i;
		if (exists($tr{$i})){
			if(ok($tr{$i})){
				delete($tr{$i});
				last;
			}
			$tr{$_}=$tr{$i} if($i ne $_);
			$i=$i1;
		}
		if($i=~s/^KP_//){
			(ok($i)||ok(lc($i))) && last;
		}
		$i=lc($i) if ($i eq $i0);
	}
	exists($k{$kc{$_}}) && next;
	exists($tr{$_}) && next;
	$un{$_}=$kc;
}

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

print "};\n\/* No symbol:\n",(map{"$_\n"} sort{$un{$a}<=>$un{$b}}keys %un),"*/\n";

print "static struct translator { char *s1,*s2; } translate[] = {\n",
	(map{"{\"$_\",\"$tr{$_}\"},\n"} sort{$a cmp $b}keys %tr),
	"};\n";

print "

#define ks2u_size $N
";


print '

int cmp_str(const void *x1, const void *x2){
	return strcmp(((struct translator *)x1)->s1,((struct translator *)x2)->s1);
}

void ks2unicode_init(){
	qsort(translate,sizeof(translate)/sizeof(struct translator),sizeof(struct translator),cmp_str);
}

static char ksText_buf[10];
static int n;

void ksText(KeySym ks, char **txt, int *is_sym){
	unsigned int wc;
	struct ks2unicode *k2u;
	int p1 = 0;
	int p2 = ks2u_size-1;
	int p;
	KeySym k;
	struct translator tr1, *tr;

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
	*is_sym=1;
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
		goto tr;
	}
	ksText_buf[n]=0;
	*txt = ksText_buf;
	goto tr;
notfound:
	*is_sym=0;
	*txt=XKeysymToString(ks);
	if (!*txt) n = sprintf(*txt = ksText_buf,"?%lx",ks);
tr:
	tr1.s1 = *txt;
	if(tr=bsearch(&tr1,translate,sizeof(translate)/sizeof(struct translator),sizeof(struct translator),cmp_str))
		*txt = tr->s2;
	return;
}

int ksText_(KeySym ks, char **txt, int *is_sym){
	ksText(ks,txt,is_sym);
	if (*txt == ksText_buf) {
		n++;
		memcpy(*txt = malloc(n), ksText_buf, n);
		return 1;
	}
	return 0;
}

';
