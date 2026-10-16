CC       ?= gcc
CXX      ?= g++
CFLAGS   = -std=c11   -O2 -Wall -Wextra -Iinclude
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Iinclude -Isrc

C_SRC   = $(wildcard src/c/*.c)
CPP_SRC = $(wildcard src/*.cpp)
OBJ     = $(patsubst src/c/%.c,build/%.o,$(C_SRC)) $(patsubst src/%.cpp,build/%.o,$(CPP_SRC))

all: relief

relief: $(OBJ)
	$(CXX) -o $@ $^

build/%.o: src/c/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build:
	mkdir -p build

test: build
	$(CC) $(CFLAGS) tests/test_dsa.c $(C_SRC) -o build/test_dsa
	./build/test_dsa

clean:
	rm -rf build relief

.PHONY: all test clean
