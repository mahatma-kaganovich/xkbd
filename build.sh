#!/bin/bash

aclocal && autoheader && libtoolize && automake -a && autoconf &&
./configure &&
make
