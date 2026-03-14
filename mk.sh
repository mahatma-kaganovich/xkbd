#!/bin/bash

cd $(dirname "$0") || exit 1

prefix=${1:-/usr/local}

(cd data && bash ./mk.sh $prefix/usr/share - *.in)
(cd src && bash ./mk.sh $1)
[ "$1" = clean ] && rm data/*.conf

