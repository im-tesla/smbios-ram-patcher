#ifndef EDITME_H
#define EDITME_H

#include "general.h"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

// ---------------------------------------------------------------------------
// SMBIOS Type 1 - System Information
// ---------------------------------------------------------------------------

// System UUID. Generate a new one with: python externals/gen_uuid.py
// Set PATCH_SYS_UUID to 0 to leave the firmware UUID untouched.
#define PATCH_SYS_UUID 1

static EFI_GUID SMBIOS_SYS_UUID UNUSED =
{
    0x36DC81D0,
    0x21D4,
    0x405A,
    { 0x8F, 0xF0, 0x85, 0x5A, 0xC0, 0x70, 0xE7, 0xDB }
};

// System serial number (Type 1, offset 0x07):
//   PATCH_SYS_SERIAL 0  -> leave untouched
//   PATCH_SYS_SERIAL 1  -> apply SMBIOS_SYS_SERIAL below
// SMBIOS_SYS_SERIAL:
//   "ABC123"  -> use this serial
//   "RANDOM"  -> generate a random 8-character serial
//   NULL      -> remove the serial string entirely
#define PATCH_SYS_SERIAL 1
static const char* SMBIOS_SYS_SERIAL UNUSED = "Default String";

// ---------------------------------------------------------------------------
// SMBIOS Type 17 - Memory Device
// ---------------------------------------------------------------------------

// Memory device serial number (Type 17, offset 0x18):
//   PATCH_MEM_SERIAL 0  -> leave untouched
//   PATCH_MEM_SERIAL 1  -> apply SMBIOS_MEM_SERIAL below
// SMBIOS_MEM_SERIAL:
//   "FCE1A2B1" -> use this serial
//   "RANDOM"   -> generate a unique random 8-character serial per slot
//   NULL       -> remove the serial string from all slots
#define PATCH_MEM_SERIAL 1
static const char* SMBIOS_MEM_SERIAL UNUSED = "EEE1321C";

// If 1, appends a slot index to the serial for multiple RAM sticks
// (e.g. FCE1A2B1, FCE1A2B12). If 0, all sticks get an identical serial.
// Ignored when SMBIOS_MEM_SERIAL is "RANDOM" or NULL.
#define SMBIOS_MEM_SERIAL_UNIQUE 0

// ---------------------------------------------------------------------------
// Console output
// ---------------------------------------------------------------------------

// Minimum level printed to the console:
//   0 = DEBUG (everything, very verbose)
//   1 = INFO  (recommended)
//   2 = SUCCESS
//   3 = WARN
//   4 = ERROR only
#define LOG_MIN_LEVEL 1

// 1 = stop at every full screen and wait for a key ("-- More --"), so long
// output can actually be read. 0 = let it scroll.
#define LOG_PAGING 1

// Seconds to wait on the final summary screen before continuing the boot.
// 0 = wait for a keypress indefinitely.
#define LOG_PAUSE_SECONDS 10

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#endif
