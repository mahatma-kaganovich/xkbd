
/* string possible temporary */
void ksText(KeySym ks, char **txt);
/* string is static. return 1 if free()-able */
int ksText_(KeySym ks, char **txt);


char *__ksText(KeySym ks){
	char *s = NULL;
	ksText(ks,&s);
	return s;
}
