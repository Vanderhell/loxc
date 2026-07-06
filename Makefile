CC      ?= cc
CXX     ?= c++
AR      ?= ar
RANLIB  ?= ranlib
PREFIX  ?= /usr/local
DESTDIR ?=
MKDIR_P ?= mkdir -p
INSTALL_DATA ?= cp -f
INSTALL_PROGRAM ?= cp -f

CPPFLAGS ?=
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
CXXFLAGS ?= -std=c++98 -Wall -Wextra -O2
LDFLAGS ?=
SANITIZE_CFLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer -g
SANITIZE_LDFLAGS ?= -fsanitize=address,undefined
ANALYZE_C ?= clang
ANALYZE_CXX ?= clang++
TIDY ?= clang-tidy
CPPCHECK ?= cppcheck
ANALYZE_GCC ?= gcc
ANALYZE_CFLAGS ?= -std=c99 -Wall -Wextra
ANALYZE_CXXFLAGS ?= -std=c++98 -Wall -Wextra
ANALYZE_GCCFLAGS ?= -std=c99 -Wall -Wextra -fanalyzer

PROJECT_CPPFLAGS := -Iinclude
MODULE_CPPFLAGS := $(PROJECT_CPPFLAGS) -Imodules
PUBLIC_HEADERS := $(wildcard include/*.h)

LIB      := libloxc.a
LIB_EMBEDDED := libloxc_embedded.a

CORE_OBJS := \
	src/loxc.o \
	src/loxc_base.o \
	src/loxc_plain.o \
	src/loxc_matrix.o \
	src/loxc_dict.o \
	src/loxc_stream.o \
	src/loxc_strategy.o \
	src/loxc_hier.o

HOST_OBJS := \
	src/loxc_loader.o \
	src/loxc_simple.o

SRC_OBJS := \
	$(CORE_OBJS) \
	$(HOST_OBJS)

TOOLS := \
	tools/loxc_train \
	tools/loxc_cli \
	tools/loxc_bench

FUZZERS := \
	fuzz/loxc_tab_fuzz

EXAMPLES := \
	examples/01_hello_world \
	examples/02_compress_file \
	examples/03_embedded_mode \
	examples/04_error_handling \
	examples/05_training_pipeline \
	examples/06_compare_modes \
	examples/07_streaming_chunks

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
	tests/test_simple \
	tests/test_loader \
	tests/test_train_demo

CONSUMERS := \
	tests/consumer_c_smoke \
	tests/consumer_cpp_smoke

ANALYZE_SRCS_C := \
	src/loxc.c \
	src/loxc_base.c \
	src/loxc_plain.c \
	src/loxc_matrix.c \
	src/loxc_dict.c \
	src/loxc_stream.c \
	src/loxc_strategy.c \
	src/loxc_hier.c \
	src/loxc_loader.c \
	src/loxc_simple.c \
	tools/loxc_cli.c \
	tools/loxc_train.c \
	fuzz/loxc_tab_fuzz.c \
	tests/consumer_c_smoke.c

ANALYZE_SRCS_CXX := \
	tests/consumer_cpp_smoke.cpp

.PHONY: all test bench examples fuzz clean host embedded package consumers sanitize analyze install uninstall

all: $(LIB) $(LIB_EMBEDDED) $(TOOLS) $(TESTS) $(CONSUMERS)

$(LIB): $(SRC_OBJS)
	$(AR) rcs $@ $^
	-$(RANLIB) $@

$(LIB_EMBEDDED): $(CORE_OBJS)
	$(AR) rcs $@ $^
	-$(RANLIB) $@

host: $(LIB)

embedded: $(LIB_EMBEDDED)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) -c -o $@ $<

tools/%: tools/%.c $(LIB)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) -o $@ $< $(LIB) $(LDFLAGS)

fuzz/%: fuzz/%.c $(LIB)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) -DLOXC_FUZZ_STANDALONE -o $@ $< $(LIB) $(LDFLAGS)

tools/loxc_bench: tools/loxc_bench.c $(LIB) modules/loxc_demo.o
	$(CC) $(CPPFLAGS) $(MODULE_CPPFLAGS) $(CFLAGS) -DLOXC_BENCH_WITH_DEMO -o $@ $< modules/loxc_demo.o $(LIB) $(LDFLAGS)

tests/%: tests/%.c $(LIB)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) -o $@ $< $(LIB) $(LDFLAGS)

tests/%: tests/%.cpp $(LIB)
	$(CXX) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CXXFLAGS) -o $@ $< $(LIB) $(LDFLAGS)

examples/%: examples/%.c $(LIB) modules/loxc_demo.loxctab
	$(CC) $(CPPFLAGS) $(MODULE_CPPFLAGS) $(CFLAGS) -o $@ $< $(LIB) $(LDFLAGS)

modules/loxc_demo.c modules/loxc_demo.h modules/loxc_demo.loxctab: tools/loxc_train trainings/demo_corpus.txt
	./tools/loxc_train --input trainings/demo_corpus.txt --output modules/loxc_demo --module-name demo --module-id 10

modules/loxc_demo.o: modules/loxc_demo.c src/loxc_stream.o src/loxc_base.o
	$(CC) $(CPPFLAGS) $(MODULE_CPPFLAGS) $(CFLAGS) -c -o $@ modules/loxc_demo.c

tests/test_train_demo: tests/test_train_demo.c $(LIB) modules/loxc_demo.o
	$(CC) $(CPPFLAGS) $(MODULE_CPPFLAGS) $(CFLAGS) -o $@ $< modules/loxc_demo.o $(LIB) $(LDFLAGS)

tests/consumer_c_smoke: tests/consumer_c_smoke.c $(LIB)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) -o $@ $< $(LIB) $(LDFLAGS)

tests/consumer_cpp_smoke: tests/consumer_cpp_smoke.cpp $(LIB)
	$(CXX) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CXXFLAGS) -o $@ $< $(LIB) $(LDFLAGS)

package: host embedded consumers

consumers: $(CONSUMERS)

sanitize:
	$(MAKE) clean
	$(MAKE) CC=$(CC) CXX=$(CXX) \
		CFLAGS="$(CFLAGS) $(SANITIZE_CFLAGS)" \
		CXXFLAGS="$(CXXFLAGS) $(SANITIZE_CFLAGS)" \
		LDFLAGS="$(LDFLAGS) $(SANITIZE_LDFLAGS)" \
		all test

analyze:
	@set -e; \
	for src in $(ANALYZE_SRCS_C); do \
	  $(TIDY) $$src -- $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(ANALYZE_CFLAGS); \
	done
	@$(CPPCHECK) --enable=warning,style,performance,portability --error-exitcode=1 -Iinclude --std=c99 $(ANALYZE_SRCS_C)
	@set -e; \
	for src in $(ANALYZE_SRCS_C); do \
	  $(ANALYZE_GCC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(ANALYZE_GCCFLAGS) -fsyntax-only $$src; \
	done
	@set -e; \
	for src in $(ANALYZE_SRCS_CXX); do \
	  $(ANALYZE_CXX) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(ANALYZE_CXXFLAGS) --analyze -o /dev/null $$src; \
	done

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
	@tests/test_simple
	@tests/test_loader
	@tests/test_train_demo

bench: tools/loxc_bench
	@tools/loxc_bench

fuzz: $(FUZZERS)

tools/loxc_bench2: tools/loxc_bench2.c $(LIB)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) -o $@ $< $(LIB) $(LDFLAGS) -lm

.PHONY: bench2 bench-full bench-clean

bench2: tools/loxc_bench2 modules/loxc_demo.loxctab
	@mkdir -p bench_out
	@if [ ! -s benchmarks/suite.list ]; then \
	  printf '%s\n' \
	    'trainings/demo_corpus.txt' \
	    'benchmarks/plain_sample_text.txt' \
	    'benchmarks/tiny.txt' 'benchmarks/small.txt' \
	    'benchmarks/medium.txt' 'benchmarks/source.c' \
	    'benchmarks/data.json' \
	    > benchmarks/suite.list; \
	fi
	./tools/loxc_bench2 \
	  --table modules/loxc_demo.loxctab \
	  --suite benchmarks/suite.list \
	  --iterations $(ITER) --warmup 3 \
	  --csv  bench_out/loxc_demo.csv \
	  --json bench_out/loxc_demo.json

bench-full: tools/loxc_bench2 tools/loxc_train
	./tools/bench_run.sh $(ITER)

bench-clean:
	rm -rf bench_out
	rm -f benchmarks/suite.list benchmarks/plain_sample_text.txt
	rm -rf benchmarks/corpora trainings/extra
	rm -f modules/loxc_json.* modules/loxc_logs.* modules/loxc_csrc.*

examples: $(EXAMPLES)

clean:
	$(RM) $(SRC_OBJS) $(LIB) $(LIB_EMBEDDED) $(TOOLS) $(TESTS) $(CONSUMERS) $(EXAMPLES) $(FUZZERS) modules/loxc_demo.o tools/loxc_bench2

install: all
	$(MKDIR_P) $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/bin
	$(INSTALL_DATA) $(PUBLIC_HEADERS) $(DESTDIR)$(PREFIX)/include/
	$(INSTALL_DATA) $(LIB) $(LIB_EMBEDDED) $(DESTDIR)$(PREFIX)/lib/
	$(INSTALL_PROGRAM) tools/loxc_cli tools/loxc_train tools/loxc_bench $(DESTDIR)$(PREFIX)/bin/

uninstall:
	$(RM) $(addprefix $(DESTDIR)$(PREFIX)/include/,$(notdir $(PUBLIC_HEADERS))) \
		$(DESTDIR)$(PREFIX)/lib/$(LIB) $(DESTDIR)$(PREFIX)/lib/$(LIB_EMBEDDED) \
		$(DESTDIR)$(PREFIX)/bin/loxc_cli $(DESTDIR)$(PREFIX)/bin/loxc_train $(DESTDIR)$(PREFIX)/bin/loxc_bench
