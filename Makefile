CC ?= gcc
OBJCOPY ?= objcopy

TARGET = hwid.efi
SO_TARGET = libhwid.so
BUILD_DIR = build

SRCS = $(wildcard src/*.c)
OBJS = $(patsubst src/%.c, $(BUILD_DIR)/%.o, $(SRCS))

CFLAGS = -DGNU_EFI_USE_MS_ABI -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -Wall -Werror -std=c11
CFLAGS += -I/usr/include/efi -I/usr/include/efi/x86_64 -I/usr/include/efi/protocol -Isrc

LDFLAGS = -nostdlib -Wl,-nostdlib -Wl,-znocombreloc -Wl,-T,/usr/lib/elf_x86_64_efi.lds -Wl,-shared -Wl,-Bsymbolic
LIBS = /usr/lib/crt0-efi-x86_64.o -L/usr/lib -lefi -lgnuefi

# .rodata MUST be listed here. The gnu-efi linker script on some distributions
# (Arch among them) emits .rodata as its own output section instead of folding
# it into .data. Leaving it out of objcopy produces an .efi whose string
# literals - every format string, every "_SM3_"/"_DMI_" anchor compared with
# CompareMem, every configured serial - read back as garbage at runtime.
EFI_SECTIONS = -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym \
               -j .rel -j .rela -j .rel.* -j .rela.* -j .reloc -j .relr.dyn

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(SO_TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(BUILD_DIR)/$(TARGET): $(BUILD_DIR)/$(SO_TARGET)
	$(OBJCOPY) $(EFI_SECTIONS) -O efi-app-x86_64 --subsystem=10 $< $@
	@./scripts/verify.sh $(BUILD_DIR)/$(SO_TARGET) $@
	@echo "[OK] Build complete: $(BUILD_DIR)/$(TARGET)"

# Host-side unit tests for the table-editing code in src/smbios.c.
TEST_CFLAGS = -std=gnu11 -g -Wall -fshort-wchar \
              -I/usr/include/efi -I/usr/include/efi/x86_64 -I/usr/include/efi/protocol -Isrc

test: $(BUILD_DIR)/smbios_test
	@$(BUILD_DIR)/smbios_test

$(BUILD_DIR)/smbios_test: tests/smbios_test.c src/smbios.c | $(BUILD_DIR)
	$(CC) $(TEST_CFLAGS) $^ -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean test
