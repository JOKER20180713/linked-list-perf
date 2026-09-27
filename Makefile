CC = gcc
CPPFLAGS = -Iinclude
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
LDFLAGS =
LDLIBS = -lm

.PHONY: all test sanitize clean native
all: build/linkbench

build:
	mkdir -p build

build/linkbench: src/main.c src/link.c src/perf_metrics.c include/link.h include/perf_metrics.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/main.c src/link.c src/perf_metrics.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_link: tests/test_link.c src/link.c include/link.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_link.c src/link.c $(LDFLAGS) -o $@

build/test_perf: tests/test_perf.c src/perf_metrics.c include/perf_metrics.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_perf.c src/perf_metrics.c $(LDFLAGS) -Wl,--wrap=syscall,--wrap=ioctl,--wrap=read,--wrap=close $(LDLIBS) -o $@

test: all build/test_link build/test_perf
	./build/test_link
	./build/test_perf
	python3 tests/test_cli.py ./build/linkbench

sanitize:
	$(MAKE) clean
	$(MAKE) test CFLAGS="-std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie" LDFLAGS="-fsanitize=address,undefined -no-pie"

native:
	$(MAKE) clean
	$(MAKE) all CFLAGS="$(CFLAGS) -march=native"

clean:
	rm -rf build
