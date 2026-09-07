# smbios-ram-patcher

UEFI application that patches SMBIOS tables in memory before the OS boots:
the system UUID (Type 1) and the memory module serial numbers (Type 17).

## Configuration

Everything is in `src/editme.h`:

| Setting | Meaning |
| --- | --- |
| `PATCH_SYS_UUID` | `1` to patch the Type 1 system UUID, `0` to leave it alone |
| `SMBIOS_SYS_UUID` | the replacement UUID (see below) |
| `PATCH_SYS_SERIAL` | `1` to patch the Type 1 system serial, `0` to leave it alone |
| `SMBIOS_SYS_SERIAL` | `"ABC123"`, `"RANDOM"`, or `NULL` to remove the string |
| `PATCH_MEM_SERIAL` | `1` to patch Type 17 serials, `0` to leave them alone |
| `SMBIOS_MEM_SERIAL` | `"FCE1A2B1"`, `"RANDOM"` (unique per slot), or `NULL` to remove |
| `SMBIOS_MEM_SERIAL_UNIQUE` | `1` appends a slot index so sticks differ, `0` gives all sticks the same serial |
| `LOG_MIN_LEVEL` | `0` DEBUG, `1` INFO (default), `2` SUCCESS, `3` WARN, `4` ERROR |
| `LOG_PAGING` | `1` stops at every full screen so long output can be read |
| `LOG_PAUSE_SECONDS` | how long the final summary stays on screen (`0` = wait for a key) |

Generate a new UUID:

```bash
python externals/gen_uuid.py
```

It prints the `EFI_GUID` block to paste into `editme.h`, plus the value
`dmidecode` and `wmic csproduct get uuid` will report, so you can verify the
patch afterwards.

## Output

Everything goes to the UEFI text console, colour coded per level
(grey debug, white info, green success, yellow warning, red error). Set
`LOG_MIN_LEVEL` to `0` for the full slot-by-slot inventory, or leave it at `1`
for a short run. With `LOG_PAGING` on, output stops at each full screen and
waits for a key, and the final summary stays up for `LOG_PAUSE_SECONDS`.

The formatter in `src/log.c` is self contained rather than a wrapper around
`Print()`, so the supported conversions (`%a %s %c %d %u %x %p %r %g`, plus
widths like `%02x`) are exactly the ones documented in `src/log.h`.

## Build

### Dependencies (Arch)

```bash
sudo pacman -S gnu-efi base-devel cmake gcc
```

### Compile

```bash
./scripts/build.sh          # cmake if available, otherwise the Makefile
```

or directly:

```bash
make                        # build/hwid.efi
make test                   # host-side unit tests for the table editing code
```

Output binary: `build/hwid.efi`

### A note on the objcopy step

The `.efi` is produced by `objcopy`-ing the linked shared object into a PE
image, and `objcopy` silently drops any section not named with `-j`. The
gnu-efi linker script shipped by some distributions (Arch included) emits
`.rodata` as its own output section instead of folding it into `.data`, so
omitting `-j .rodata` yields an image that links and boots but whose string
literals all read back as garbage: no console output, no `_SM3_`/`_DMI_`
anchor matches, and an empty configured serial.

`scripts/verify.sh` runs after every build and fails if any allocated section
did not make it into the image, so this cannot regress silently.

## Usage

1. Copy `build/hwid.efi` to a FAT32 flash drive or the EFI system partition.
2. Launch it from the UEFI Shell, or chainload it before booting the OS.

Verify afterwards from the booted OS:

```bash
sudo dmidecode -t 1        # system UUID and serial
sudo dmidecode -t 17       # memory module serial numbers
```

## How the patching works

* Entry points are located through the EFI configuration table (SMBIOS 3.x
  first, then 2.x), the HOB list, and finally a legacy scan of `0xF0000`.
  Entry points that share a structure table are merged into one context so
  both get their checksums fixed.
* The length of a structure table is *measured* by walking to the Type 127
  terminator rather than trusted from the entry point, because a SMBIOS 3
  entry point reports a maximum size that is usually larger than the table.
* Editing a string in place is only done when it fits. Growing past the end of
  the firmware's buffer relocates the whole table into freshly allocated
  `EfiRuntimeServicesData` (below 4 GB when a 2.x entry point is present,
  since it stores the address in a `UINT32`), and the entry points are
  repointed at it.
* Removing a string shifts down every string index in the same structure that
  pointed past it.
* Entry point checksums, table length, structure count and maximum structure
  size are recomputed after every edit.

## Requirements

Secure Boot off, or a Secure Boot bypass.
