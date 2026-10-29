CC ?= cc
ARM_CC ?= arm-none-eabi-gcc
ARM_NM ?= arm-none-eabi-nm
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror
CORE = krawall.c discovery.c mangle_adapter.c pcm_mixer.c
.PHONY: all test arm clean
all: build/krawplay
build:
	mkdir -p build
build/krawplay: krawplay.c $(CORE) krawall.h mangle_adapter.h pcm_mixer.h | build
	$(CC) $(CFLAGS) -DHOST krawplay.c $(CORE) -o $@
build/test_player: tests/test_player.c $(CORE) krawall.h mangle_adapter.h pcm_mixer.h | build
	$(CC) $(CFLAGS) -DHOST -g -fsanitize=address,undefined -fno-sanitize-recover=all tests/test_player.c $(CORE) -o $@
test: build/test_player
	./build/test_player
arm: | build
	$(ARM_CC) $(CFLAGS) -mcpu=arm7tdmi -marm -ffreestanding -fno-builtin -c krawall.c -o build/krawall.o
	$(ARM_CC) $(CFLAGS) -mcpu=arm7tdmi -marm -ffreestanding -fno-builtin -c mangle_adapter.c -o build/mangle_adapter.o
	$(ARM_CC) $(CFLAGS) -mcpu=arm7tdmi -marm -ffreestanding -fno-builtin -c pcm_mixer.c -o build/pcm_mixer.o
	$(ARM_CC) $(CFLAGS) -mcpu=arm7tdmi -marm -ffreestanding -fno-builtin -c discovery.c -o build/discovery.o
	$(ARM_CC) $(CFLAGS) -mcpu=arm7tdmi -marm -ffreestanding -fno-builtin -c integration_example.c -o build/integration_example.o
	$(ARM_CC) -nostdlib -r build/krawall.o build/mangle_adapter.o build/pcm_mixer.o build/discovery.o build/integration_example.o -o build/krawall_poc.o
	$(ARM_NM) -u build/krawall_poc.o
	test -z "$$($(ARM_NM) -u build/krawall_poc.o)"
clean:
	rm -rf build
