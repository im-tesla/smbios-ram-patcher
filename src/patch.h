#ifndef PATCH_H
#define PATCH_H

#include "general.h"
#include "smbios.h"

typedef struct {
    UINTN Type1Patched;      // System Information structures changed
    UINTN Type17Patched;     // Memory Device slots changed
    UINTN Type17Skipped;     // Memory Device slots left alone (empty)
    UINTN Failures;          // Edits that were attempted and failed
} PATCH_STATS;

BOOLEAN PatchType1(SMBIOS_CONTEXT* ctx, PATCH_STATS* stats);
BOOLEAN PatchType17(SMBIOS_CONTEXT* ctx, PATCH_STATS* stats);
BOOLEAN PatchOneContext(SMBIOS_CONTEXT* ctx, PATCH_STATS* stats);
BOOLEAN PatchAll(SMBIOS_CONTEXT_LIST* list, PATCH_STATS* stats);

#endif
