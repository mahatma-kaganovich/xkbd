
/* string possible temporary */
void ksText(KeySym ks, char **txt, int *is_sym);
/* string is static. return 1 if free()-able */
int ksText_(KeySym ks, char **txt, int *is_sym);


char *__ksText(KeySym ks){
	char *s = NULL;
	int sym;
	ksText(ks,&s,&sym);
	return s;
}

void ks2unicode_init();
