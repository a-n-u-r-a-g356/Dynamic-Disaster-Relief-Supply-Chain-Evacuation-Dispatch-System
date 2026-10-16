#!/bin/sh
# Clean build for Linux/macOS/MSYS2 shell:  ./build.sh [run|test]
set -e
cd "$(dirname "$0")"
rm -rf build && mkdir -p build/obj
for f in src/c/*.c; do gcc -std=c11 -Wall -Wextra -Iinclude -c "$f" -o "build/obj/$(basename "$f" .c).o"; done
for f in src/*.cpp;  do g++ -std=c++17 -Wall -Wextra -Iinclude -Isrc -c "$f" -o "build/obj/$(basename "$f" .cpp).o"; done
g++ -o build/relief build/obj/*.o
echo "BUILD OK -> build/relief"
if [ "$1" = "test" ]; then
    gcc -std=c11 -Wall -Iinclude tests/test_dsa.c build/obj/graph.o build/obj/hashmap.o build/obj/heap.o build/obj/queue.o -o build/test_dsa
    ./build/test_dsa
    ./build/relief --script tests/session_robust.txt > /dev/null && echo "robustness session ran"
    ./build/relief --script tests/session.txt > /dev/null && echo "main session ran"
    python3 -I tests/test_fetcher.py
fi
[ "$1" = "run" ] && exec ./build/relief
exit 0
