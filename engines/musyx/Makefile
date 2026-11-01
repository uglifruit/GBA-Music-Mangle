CC ?= cc
ARM_CC ?= arm-none-eabi-gcc
ARM_NM ?= arm-none-eabi-nm
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror
CORE = musyx.c mixer_adapter.c pcm_mixer.c
.PHONY: all test arm trial clean
all: build/musyxplay
build:
	mkdir -p build
build/musyxplay: musyxplay.c $(CORE) musyx.h pcm_mixer.h mixer_adapter.h macro_lengths.h | build
	$(CC) $(CFLAGS) -DHOST musyxplay.c $(CORE) -o $@
build/test_player: tests/test_player.c $(CORE) musyx.h pcm_mixer.h mixer_adapter.h macro_lengths.h demo_data.h | build
	$(CC) $(CFLAGS) -DHOST -g -fsanitize=address,undefined -fno-sanitize-recover=all tests/test_player.c $(CORE) -o $@
test: build/test_player
	./build/test_player
arm: | build
	$(ARM_CC) $(CFLAGS) -mcpu=arm7tdmi -marm -ffreestanding -fno-builtin -c musyx.c -o build/musyx.o
	$(ARM_CC) $(CFLAGS) -mcpu=arm7tdmi -marm -ffreestanding -fno-builtin -c mixer_adapter.c -o build/mixer_adapter.o
	$(ARM_CC) $(CFLAGS) -mcpu=arm7tdmi -marm -ffreestanding -fno-builtin -c pcm_mixer.c -o build/pcm_mixer.o
	$(ARM_CC) -nostdlib -r build/musyx.o build/mixer_adapter.o build/pcm_mixer.o -o build/musyx_poc.o
	$(ARM_NM) -u build/musyx_poc.o
	test -z "$$($(ARM_NM) -u build/musyx_poc.o)"
trial:
	bash trial/build.sh
clean:
	rm -rf build

.PHONY: demo
demo: build/musyxplay
	python tests/make_demo.py build/demo.gba
	./build/musyxplay render build/demo.gba 0x08000100 0 build/demo.wav 5 strict
