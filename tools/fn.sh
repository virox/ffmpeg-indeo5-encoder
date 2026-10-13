#!/bin/sh
# print decompiled function(s) by address/name from re/ir50_32_decomp.c
D=$(dirname "$0")/../re/ir50_32_decomp.c
for a in "$@"; do
  awk -v a="$a" '/^\/\* ==== /{p = (index($0, a) > 0)} p' "$D"
done
