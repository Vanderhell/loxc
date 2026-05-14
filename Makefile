CC      ?= cc
AR      ?= ar
RANLIB  ?= ranlib

CFLAGS  ?= -std=c99 -Wall -Wextra -O2 -Iinclude
LDFLAGS ?=

LIB      := libloxc.a
SRC_OBJS := \
	src/loxc.o \
	src/loxc_base.o \
	src/loxc_plain.o \
	src/loxc_matrix.o \
	src/loxc_dict.o \
	src/loxc_stream.o \
	src/loxc_strategy.o \
	src/loxc_hier.o \
	src/loxc_loader.o

TOOLS := \
	tools/loxc_train \
	tools/loxc_cli \
	tools/loxc_bench

EXAMPLES := \
	examples/01_basic_compress \
	examples/02_embedded_mode \
	examples/03_train_and_use

TESTS := \
	tests/test_basic \
	tests/test_matrix \
	tests/test_dict \
	tests/test_stream \
	tests/test_header \
	tests/test_plain \
	tests/test_strategy \
	tests/test_hier \
	tests/test_registry \
	tests/test_loader \
	tests/test_train_demo

.PHONY: all test bench examples clean

all: $(LIB) $(TOOLS) $(TESTS)

$(LIB): $(SRC_OBJS)
	$(AR) rcs $@ $^
	-$(RANLIB) $@

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

tools/%: tools/%.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) $(LDFLAGS)

tools/loxc_bench: tools/loxc_bench.c $(LIB) modules/loxc_demo.o
	$(CC) $(CFLAGS) -Imodules -DLOXC_BENCH_WITH_DEMO -o $@ $< modules/loxc_demo.o $(LIB) $(LDFLAGS)

tests/%: tests/%.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) $(LDFLAGS)

examples/%: examples/%.c $(LIB) modules/loxc_demo.loxctab
	$(CC) $(CFLAGS) -o $@ $< $(LIB) $(LDFLAGS)

modules/loxc_demo.c modules/loxc_demo.h: tools/loxc_train trainings/demo_corpus.txt
	./tools/loxc_train --input trainings/demo_corpus.txt --output modules/loxc_demo --module-name demo --module-id 10

modules/loxc_demo.o: modules/loxc_demo.c src/loxc_stream.o src/loxc_base.o
	$(CC) $(CFLAGS) -Imodules -c -o $@ modules/loxc_demo.c

tests/test_train_demo: tests/test_train_demo.c $(LIB) modules/loxc_demo.o
	$(CC) $(CFLAGS) -Imodules -o $@ $< modules/loxc_demo.o $(LIB) $(LDFLAGS)

test: $(TESTS)
	@tests/test_basic
	@tests/test_matrix
	@tests/test_dict
	@tests/test_stream
	@tests/test_header
	@tests/test_plain
	@tests/test_strategy
	@tests/test_hier
	@tests/test_registry
	@tests/test_loader
	@tests/test_train_demo

bench: tools/loxc_bench
	@tools/loxc_bench

examples: $(EXAMPLES)

clean:
	$(RM) $(SRC_OBJS) $(LIB) $(TOOLS) $(TESTS) $(EXAMPLES) modules/loxc_demo.o
