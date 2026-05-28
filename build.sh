#!/bin/bash

#aclocal && autoheader && libtoolize && automake -a && autoconf &&
autoreconf -i &&
./configure &&
make
