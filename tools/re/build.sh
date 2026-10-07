#!/bin/sh
# Rebuild the research database from the original SLIPSTRM.EXE (read-only input).
#   SLIP_EXE=/path/to/SLIPSTRM.EXE CAPSTONE_LIB=/path/to/libcapstone.dylib ./build.sh
set -e
cd "$(dirname "$0")"
mkdir -p work
python3 disc.py          # recursive-descent discovery (pass 1)
python3 disc2.py         # gap/orphan discovery with validated tentative decoding (pass 2)
python3 db.py            # function registry, call graph, data xrefs -> work/db.pkl
python3 xr.py > work/func_strings.txt
python3 lin.py 10000 6e000 > work/listing.txt   # annotated listing of the whole code object
echo "done: work/listing.txt, work/db.pkl"
