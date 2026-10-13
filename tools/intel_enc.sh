#!/bin/sh
# intel_enc.sh name W H input.bgr [key=value ...] : encode with IR50_32.DLL, summarise and check decoders
D=$(cd "$(dirname "$0")/.." && pwd)
name=$1; w=$2; h=$3; in=$4; shift 4
out=$D/re/enc/$name.ivr
log=$("$D/tools/iv5ref/iv5ref" enc "$D/IR50_32.DLL" "$in" "$w" "$h" "$out" "$@" 2>&1)
echo "== $name: $*"
echo "$log" | grep -E "SETSTATE|GETSTATE|COMPRESS ->|failed|unimplemented|frame [0-9]+:" | sed 's/^/  /'
[ -s "$out" ] && python3 -I -c "
import importlib.util; s=importlib.util.spec_from_file_location('s','$D/tools/iv5stat.py'); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); m.summary('$out')"
