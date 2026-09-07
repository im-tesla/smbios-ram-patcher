#!/bin/bash
# Sanity-checks the objcopy step that turns the shared object into a PE image.
#
# objcopy silently drops any section that was not passed with -j. A missing
# .rodata (which is where every string literal lives) produces an .efi that
# links and boots but reads garbage for every string - no console output, no
# "_SM3_"/"_DMI_" anchor matches, no configured serial. Catch that at build
# time instead of on the target machine.
#
# Usage: verify.sh <input.so> <output.efi>

set -u

SO_FILE="${1:?usage: verify.sh <input.so> <output.efi>}"
EFI_FILE="${2:?usage: verify.sh <input.so> <output.efi>}"

# Allocated sections a gnu-efi application does not need at runtime.
IGNORED_RE='^(\.gnu\.hash|\.hash|\.eh_frame|\.eh_frame_hdr|\.gcc_except_table|\.dynstr|\.note\..*|\.comment)$'

# Lists the names of allocated, non-empty sections of an object file.
list_alloc_sections() {
    objdump -h "$1" | awk '
        $1 ~ /^[0-9]+$/ && $2 ~ /^\./ { name = $2; size = $3; next }
        name != "" {
            if ($0 ~ /ALLOC/ && size !~ /^0+$/) print name
            name = ""
        }
    '
}

elf_sections=$(list_alloc_sections "$SO_FILE")
pe_sections=$(list_alloc_sections "$EFI_FILE")

if [ -z "$elf_sections" ] || [ -z "$pe_sections" ]; then
    echo "[ERROR] Could not read section headers from $SO_FILE / $EFI_FILE" >&2
    exit 1
fi

missing=""
while read -r s; do
    [ -n "$s" ] || continue
    [[ "$s" =~ $IGNORED_RE ]] && continue
    grep -qx -- "$s" <<< "$pe_sections" || missing="$missing $s"
done <<< "$elf_sections"

if [ -n "$missing" ]; then
    echo "[ERROR] objcopy dropped allocated section(s) from $EFI_FILE:$missing" >&2
    echo "        Add the matching -j flags to EFI_SECTIONS in the Makefile" >&2
    echo "        (and to the objcopy command in CMakeLists.txt)." >&2
    exit 1
fi

exit 0
