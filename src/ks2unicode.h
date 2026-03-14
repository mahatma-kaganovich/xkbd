
/* string possible temporary */
void ksText(KeySym ks, char **txt, int *is_sym);
/* string is static. return 1 if free()-able */
int ksText_(KeySym ks, char **txt, int *is_sym);

void ks2unicode_init();
