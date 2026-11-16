#!/bin/bash
# Times compile_kinds.cc built with 16, 64 and 256 kinds, the schedule's
# check on and off, in the build directory given (configured already, its
# modules built): what the check costs while compiling, and how it grows.
#   bench/layout/compile_time.sh build-bench
set -e
dir=${1:?the build directory}
printf '%-6s %-8s %10s\n' kinds checked seconds
for kinds in 16 64 256; do
  for checked in 0 1; do
    target=layout_compile_k${kinds}_c${checked}
    rm -f "$dir/CMakeFiles/$target.dir/src/compile_kinds.cc.o"
    start=$(date +%s.%N)
    cmake --build "$dir" --target "$target" -j1 > /dev/null
    end=$(date +%s.%N)
    printf "%-6s %-8s %10.2f\n" "$kinds" "$checked" "$(python3 -c "print($end - $start)")"
  done
done
