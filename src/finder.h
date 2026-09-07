#ifndef FINDER_H
#define FINDER_H

#include "general.h"
#include "smbios.h"

// Locates every SMBIOS entry point on the system (configuration table, HOB
// list, legacy F-segment scan) and fills `list` with one context per distinct
// structure table.
BOOLEAN FindAllSmbios(SMBIOS_CONTEXT_LIST* list);

// Validates a SMBIOS 2.x / 3.x entry point (anchor strings + checksums).
BOOLEAN CheckEntry2(SMBIOS_STRUCTURE_TABLE* entry);
BOOLEAN CheckEntry3(SMBIOS_3_0_ENTRY_POINT* entry);

void* FindByHob(VOID);
void* FindBySignature(VOID);

#endif
