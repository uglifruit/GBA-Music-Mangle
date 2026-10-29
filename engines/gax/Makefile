CC ?= cc
ARM_CC ?= arm-none-eabi-gcc
ARM_NM ?= arm-none-eabi-nm
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror
CORE = gax.c mangle_adapter.c pcm_mixer.c
.PHONY: all test arm trial clean
all: build/gaxplay
build:
	mkdir -p build
build/gaxplay: gaxplay.c $(CORE) gax.h pcm_mixer.h mangle_adapter.h | build
	$(CC) $(CFLAGS) -DHOST gaxplay.c $(CORE) -o $@
build/test_player: tests/test_player.c $(CORE) gax.h pcm_mixer.h mangle_adapter.h | build
	$(CC) $(CFLAGS) -DHOST -g -fsanitize=address,undefined -fno-sanitize-recover=all tests/test_player.c $(CORE) -o $@
test: build/test_player
	./build/test_player
arm: | build
	$(ARM_CC) $(CFLAGS) -mcpu=arm7tdmi -marm -ffreestanding -fno-builtin -c gax.c -o build/gax.o
	$(ARM_CC) $(CFLAGS) -mcpu=arm7tdmi -marm -ffreestanding -fno-builtin -c mangle_adapter.c -o build/mangle_adapter.o
	$(ARM_CC) $(CFLAGS) -mcpu=arm7tdmi -marm -ffreestanding -fno-builtin -c pcm_mixer.c -o build/pcm_mixer.o
	$(ARM_CC) -nostdlib -r build/gax.o build/mangle_adapter.o build/pcm_mixer.o -o build/gax_poc.o
	$(ARM_NM) -u build/gax_poc.o
	test -z "$$($(ARM_NM) -u build/gax_poc.o)"
trial:
	bash trial/build.sh
clean:
	rm -rf build
