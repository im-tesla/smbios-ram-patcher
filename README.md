# smbios-ram-patcher

UEFI application to patch SMBIOS table entries in memory before OS boot (System UUID & RAM serial numbers).

## Configuration

Edit values in `src/editme.h`:
- `SMBIOS_SYS_UUID` (Type 1 - System Information)
- `SMBIOS_MEM_SERIAL` (Type 17 - Memory Device)

Generate a new UUID:
```bash
python externals/gen_uuid.py
```

## Build

### Dependencies (Arch)
```bash
sudo pacman -S gnu-efi base-devel cmake gcc
```

### Compile
```bash
./scripts/build.sh
```
Output binary: `build/hwid.efi`

## Usage

1. Copy `build/hwid.efi` to a FAT32 flash drive or EFI system partition.
2. Launch it via UEFI Shell or chainload before booting the OS.

## Requirements
Secure Boot OFF / Secure Boot Bypass (can be done with UEFI firmware modification)