#ifndef FINDER_H
#define FINDER_H

#include "general.h"
#include "smbios.h"

BOOLEAN FindSmbios(SMBIOS_CONTEXT* ctx);

// Backwards compatibility declarations
int CheckEntry(SMBIOS_STRUCTURE_TABLE* entry);
void* FindBySignature(VOID);
void* FindByHob(VOID);
void* FindByConfig(VOID);
SMBIOS_STRUCTURE_TABLE* FindEntry(VOID);

#endif