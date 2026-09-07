#ifndef PATCH_H
#define PATCH_H

#include "general.h"
#include "smbios.h"

BOOLEAN PatchType1(SMBIOS_CONTEXT* ctx);
UINTN PatchType17(SMBIOS_CONTEXT* ctx);
BOOLEAN PatchAll(SMBIOS_CONTEXT* ctx);
UINTN GetLogPauseSeconds(VOID);

#endif
