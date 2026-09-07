cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_COMPILER /usr/bin/gcc)

project(hwid C)

set(EFIBIN "hwid.efi")

include_directories(/usr/include/efi)
include_directories(/usr/include/efi/x86_64)
include_directories(/usr/include/efi/protocol)
include_directories(${CMAKE_SOURCE_DIR}/src)

link_directories(/usr/lib)

set(COMMONFLAGS "-DGNU_EFI_USE_MS_ABI -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -Wall -Werror")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --std=c11 ${COMMONFLAGS}")

file(GLOB_RECURSE sourcefiles
    "src/*.h"
    "src/*.c"
)

add_library(hwid SHARED ${sourcefiles})

# Keep this identical to LDFLAGS/LIBS in the Makefile so both build paths
# produce the same image. -nostdlib has to reach the compiler driver (not just
# the linker) or gcc links crti/crtn and the image grows .init/.fini sections
# that a gnu-efi application never runs.
target_link_options(hwid PRIVATE
    -nostdlib
    -Wl,-nostdlib
    -Wl,-znocombreloc
    -Wl,-T,/usr/lib/elf_x86_64_efi.lds
    -Wl,-Bsymbolic
)
target_link_libraries(hwid PRIVATE /usr/lib/crt0-efi-x86_64.o -L/usr/lib -lefi -lgnuefi)

# .rodata MUST be in this list. The gnu-efi linker script on some distributions
# (Arch among them) emits .rodata as its own output section instead of folding
# it into .data; without -j .rodata objcopy silently drops every string literal
# from the image. See scripts/verify.sh.
add_custom_command(
    TARGET hwid POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
            -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym
            -j .rel -j .rela -j .rel.* -j .rela.* -j .reloc -j .relr.dyn
            -O efi-app-x86_64 --subsystem=10   # efi-rtdrv for a runtime driver
            libhwid.so ${EFIBIN}
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/verify.sh libhwid.so ${EFIBIN}
    VERBATIM
)
