rm {,*/}Makefile{,.in};aclocal && autoheader && libtoolize && automake -a && autoconf;./configure && make -j
