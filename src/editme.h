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

// python externals/gen_uuid.py
static EFI_GUID SMBIOS_SYS_UUID UNUSED =
{
    0x57AE3734,
    0xEB52,
    0x764C,
    { 0x84, 0xF0, 0x24, 0x71, 0x4F, 0xAF, 0x4F, 0xD4 }
};

#define SMBIOS_SYS_SERIAL NULL

static const char* SMBIOS_MEM_SERIAL UNUSED = "FCE1A2B1";

#define LOG_PAUSE_SECONDS 5

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#endif