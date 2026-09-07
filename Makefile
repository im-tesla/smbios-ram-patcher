CC ?= gcc
OBJCOPY ?= objcopy

TARGET = hwid.efi
SO_TARGET = libhwid.so
BUILD_DIR = build

SRCS = $(wildcard src/*.c)
OBJS = $(patsubst src/%.c, $(BUILD_DIR)/%.o, $(SRCS))

CFLAGS = -DGNU_EFI_USE_MS_ABI -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -Wall -Werror -std=c11
CFLAGS += -I/usr/include/efi -I/usr/include/efi/x86_64 -I/usr/include/efi/protocol -Isrc

LDFLAGS = -Wl,-nostdlib -Wl,-znocombreloc -Wl,-T,/usr/lib/elf_x86_64_efi.lds -Wl,-shared -Wl,-Bsymbolic
LIBS = /usr/lib/crt0-efi-x86_64.o -L/usr/lib -lefi -lgnuefi

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(SO_TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(BUILD_DIR)/$(TARGET): $(BUILD_DIR)/$(SO_TARGET)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc \
		-O efi-app-x86_64 $< $@
	@echo "[OK] Build complete: $(BUILD_DIR)/$(TARGET)"

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
