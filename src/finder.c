#include "general.h"
#include "finder.h"
#include "smbios.h"
#include "edk2/PiHob.h"
#include "hob.h"
#include "log.h"

EFI_GUID SmbiosTableGuid  = { 0xEB9D2D31, 0x2D88, 0x11D3, { 0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D } };
EFI_GUID Smbios3TableGuid = { 0xF2FD1544, 0x9794, 0x4A2C, { 0x99, 0x2E, 0xE5, 0xBB, 0xCF, 0x20, 0xE3, 0x94 } };

#define GET_GUID_HOB_DATA(GuidHob) ((VOID*) (((UINT8*) &((GuidHob)->Name)) + sizeof (EFI_GUID)))

BOOLEAN CheckEntry2(SMBIOS_STRUCTURE_TABLE* entry)
{
    if (!entry || entry->EntryPointLength < 0x1F)
        return FALSE;

    if (CompareMem(entry->AnchorString, "_SM_", 4) != 0)
        return FALSE;

    if (CompareMem(entry->IntermediateAnchorString, "_DMI_", 5) != 0)
        return FALSE;

    const UINT8* p = (const UINT8*)entry;
    UINT8 sum = 0;
    for (UINTN i = 0; i < entry->EntryPointLength; i++)
        sum = (UINT8)(sum + p[i]);

    if (sum != 0)
        return FALSE;

    sum = 0;
    const UINT8* ip = (const UINT8*)entry + 0x10;
    for (UINTN i = 0; i < 0x10; i++)
        sum = (UINT8)(sum + ip[i]);

    return (sum == 0);
}

BOOLEAN CheckEntry3(SMBIOS_3_0_ENTRY_POINT* entry)
{
    if (!entry || entry->EntryPointLength < 0x18)
        return FALSE;

    if (CompareMem(entry->AnchorString, "_SM3_", 5) != 0)
        return FALSE;

    const UINT8* p = (const UINT8*)entry;
    UINT8 sum = 0;
    for (UINTN i = 0; i < entry->EntryPointLength; i++)
        sum = (UINT8)(sum + p[i]);

    return (sum == 0);
}

void* FindBySignature(VOID)
{
    for (UINTN offset = 0xF0000; offset < 0x100000; offset += 0x10)
    {
        if (CompareMem((VOID*)offset, "_SM3_", 5) == 0)
        {
            if (CheckEntry3((SMBIOS_3_0_ENTRY_POINT*)offset))
                return (VOID*)offset;
        }
        else if (CompareMem((VOID*)offset, "_SM_", 4) == 0)
        {
            if (CheckEntry2((SMBIOS_STRUCTURE_TABLE*)offset))
                return (VOID*)offset;
        }
    }

    return NULL;
}

void* FindByHob(VOID)
{
    EFI_PEI_HOB_POINTERS guidHob;

    guidHob.Raw = GetFirstGuidHob(&Smbios3TableGuid);
    if (guidHob.Raw != NULL)
    {
        EFI_PHYSICAL_ADDRESS* table = (EFI_PHYSICAL_ADDRESS*)GET_GUID_HOB_DATA(guidHob.Guid);
        if (table != NULL && *table != 0)
            return (VOID*)(UINTN)(*table);
    }

    guidHob.Raw = GetFirstGuidHob(&SmbiosTableGuid);
    if (guidHob.Raw != NULL)
    {
        EFI_PHYSICAL_ADDRESS* table = (EFI_PHYSICAL_ADDRESS*)GET_GUID_HOB_DATA(guidHob.Guid);
        if (table != NULL && *table != 0)
            return (VOID*)(UINTN)(*table);
    }

    return NULL;
}

static SMBIOS_CONTEXT* FindContextByTable(SMBIOS_CONTEXT_LIST* list, UINT8* tableAddress)
{
    for (UINTN i = 0; i < list->Count; i++)
    {
        if (list->Contexts[i].TableAddress == tableAddress)
            return &list->Contexts[i];
    }

    return NULL;
}

// Determines the real extent of a structure table. `declaredSize` is what the
// entry point claims; it is only used to bound the walk. The value actually
// stored is the size measured by walking to the Type 127 terminator, because
// SMBIOS 3 entry points report a *maximum* size that is frequently larger than
// the table, and writing past the real end would corrupt unrelated memory.
static BOOLEAN MeasureTable(SMBIOS_CONTEXT* ctx, UINT32 declaredSize)
{
    UINT32 scanLimit = (declaredSize > 0 && declaredSize <= SMBIOS_MAX_TABLE_SIZE)
                           ? declaredSize
                           : SMBIOS_MAX_TABLE_SIZE;

    UINT16 count = 0;
    UINT32 measured = SmbiosGetTotalTableSize(ctx->TableAddress, scanLimit, &count);

    if (measured == 0 && scanLimit != SMBIOS_MAX_TABLE_SIZE)
    {
        // The declared size was too small to reach the terminator; retry
        // without trusting it.
        measured = SmbiosGetTotalTableSize(ctx->TableAddress, SMBIOS_MAX_TABLE_SIZE, &count);
    }

    if (measured == 0)
    {
        LOG_WARN(L"Structure table at 0x%lx has no valid Type 127 terminator (declared %u bytes)\n",
                 (UINT64)(UINTN)ctx->TableAddress, declaredSize);
        return FALSE;
    }

    ctx->TableLength = measured;
    // The firmware buffer carries no guaranteed headroom: treat it as exactly
    // full so that any growth forces a relocation into memory we own.
    ctx->TableMaxAlloc = measured;
    ctx->NumStructures = count;
    ctx->IsDynamicAlloc = FALSE;

    return TRUE;
}

static BOOLEAN AddContext3(SMBIOS_CONTEXT_LIST* list, SMBIOS_3_0_ENTRY_POINT* ep3, CONST CHAR16* source)
{
    UINT8* tableAddr = (UINT8*)(UINTN)ep3->TableAddress;

    if (tableAddr == NULL)
        return FALSE;

    SMBIOS_CONTEXT* existing = FindContextByTable(list, tableAddr);
    if (existing != NULL)
    {
        if (existing->EntryPoint3 == NULL)
        {
            existing->EntryPoint3 = ep3;
            LOG_DEBUG(L"  Additional SMBIOS 3.x entry point at 0x%lx (%s) shares this table\n",
                      (UINT64)(UINTN)ep3, source);
        }
        return FALSE;
    }

    if (list->Count >= MAX_SMBIOS_CONTEXTS)
        return FALSE;

    SMBIOS_CONTEXT* ctx = &list->Contexts[list->Count];
    ZeroMem(ctx, sizeof(*ctx));
    ctx->Version = SMBIOS_VERSION_3;
    ctx->EntryPoint3 = ep3;
    ctx->TableAddress = tableAddr;

    if (!MeasureTable(ctx, ep3->TableMaximumSize))
        return FALSE;

    list->Count++;

    LOG_SUCCESS(L"Located SMBIOS %u.%u via %s\n", ep3->MajorVersion, ep3->MinorVersion, source);
    LOG_DEBUG(L"  Entry point:   0x%lx (rev %u, docrev %u)\n",
              (UINT64)(UINTN)ep3, ep3->EntryPointRevision, ep3->DocRev);
    LOG_DEBUG(L"  Table address: 0x%lx  declared max %u bytes, measured %u bytes, %u structures\n",
              (UINT64)(UINTN)ctx->TableAddress, ep3->TableMaximumSize,
              ctx->TableLength, ctx->NumStructures);
    return TRUE;
}

static BOOLEAN AddContext2(SMBIOS_CONTEXT_LIST* list, SMBIOS_STRUCTURE_TABLE* ep2, CONST CHAR16* source)
{
    UINT8* tableAddr = (UINT8*)(UINTN)ep2->TableAddress;

    if (tableAddr == NULL)
        return FALSE;

    SMBIOS_CONTEXT* existing = FindContextByTable(list, tableAddr);
    if (existing != NULL)
    {
        if (existing->EntryPoint2 == NULL)
        {
            existing->EntryPoint2 = ep2;
            LOG_SUCCESS(L"Located SMBIOS %u.%u via %s (shares the table already found)\n",
                        ep2->MajorVersion, ep2->MinorVersion, source);
            LOG_DEBUG(L"  Entry point: 0x%lx, %u structures\n",
                      (UINT64)(UINTN)ep2, ep2->NumberOfSmbiosStructures);
        }
        return FALSE;
    }

    if (list->Count >= MAX_SMBIOS_CONTEXTS)
        return FALSE;

    SMBIOS_CONTEXT* ctx = &list->Contexts[list->Count];
    ZeroMem(ctx, sizeof(*ctx));
    ctx->Version = SMBIOS_VERSION_2;
    ctx->EntryPoint2 = ep2;
    ctx->TableAddress = tableAddr;

    if (!MeasureTable(ctx, ep2->TableLength))
        return FALSE;

    list->Count++;

    LOG_SUCCESS(L"Located SMBIOS %u.%u via %s\n", ep2->MajorVersion, ep2->MinorVersion, source);
    LOG_DEBUG(L"  Entry point:   0x%lx\n", (UINT64)(UINTN)ep2);
    LOG_DEBUG(L"  Table address: 0x%lx  declared %u bytes, measured %u bytes, %u structures\n",
              (UINT64)(UINTN)ctx->TableAddress, ep2->TableLength,
              ctx->TableLength, ctx->NumStructures);
    return TRUE;
}

BOOLEAN FindAllSmbios(SMBIOS_CONTEXT_LIST* list)
{
    if (!list)
        return FALSE;

    ZeroMem(list, sizeof(SMBIOS_CONTEXT_LIST));

    VOID* table = NULL;

    // 1. SMBIOS 3.x entry point in the EFI configuration table.
    if (LibGetSystemConfigurationTable(&Smbios3TableGuid, &table) == EFI_SUCCESS && table != NULL)
    {
        if (CheckEntry3((SMBIOS_3_0_ENTRY_POINT*)table))
            AddContext3(list, (SMBIOS_3_0_ENTRY_POINT*)table, L"EFI configuration table");
        else
            LOG_WARN(L"SMBIOS 3.x configuration table entry at 0x%lx failed validation\n",
                     (UINT64)(UINTN)table);
    }

    // 2. SMBIOS 2.x entry point in the EFI configuration table.
    table = NULL;
    if (LibGetSystemConfigurationTable(&SmbiosTableGuid, &table) == EFI_SUCCESS && table != NULL)
    {
        if (CheckEntry2((SMBIOS_STRUCTURE_TABLE*)table))
            AddContext2(list, (SMBIOS_STRUCTURE_TABLE*)table, L"EFI configuration table");
        else
            LOG_WARN(L"SMBIOS 2.x configuration table entry at 0x%lx failed validation\n",
                     (UINT64)(UINTN)table);
    }

    // 3. HOB list (some firmwares publish the entry point only there).
    VOID* hobTable = FindByHob();
    if (hobTable != NULL)
    {
        if (CheckEntry3((SMBIOS_3_0_ENTRY_POINT*)hobTable))
            AddContext3(list, (SMBIOS_3_0_ENTRY_POINT*)hobTable, L"HOB list");
        else if (CheckEntry2((SMBIOS_STRUCTURE_TABLE*)hobTable))
            AddContext2(list, (SMBIOS_STRUCTURE_TABLE*)hobTable, L"HOB list");
        else
            LOG_DEBUG(L"HOB SMBIOS pointer 0x%lx is not a recognised entry point\n",
                      (UINT64)(UINTN)hobTable);
    }

    // 4. Legacy F-segment scan (0xF0000 - 0xFFFFF).
    VOID* scanTable = FindBySignature();
    if (scanTable != NULL)
    {
        if (CheckEntry3((SMBIOS_3_0_ENTRY_POINT*)scanTable))
            AddContext3(list, (SMBIOS_3_0_ENTRY_POINT*)scanTable, L"legacy memory scan");
        else if (CheckEntry2((SMBIOS_STRUCTURE_TABLE*)scanTable))
            AddContext2(list, (SMBIOS_STRUCTURE_TABLE*)scanTable, L"legacy memory scan");
    }

    if (list->Count == 0)
    {
        LOG_ERROR(L"Could not locate a usable SMBIOS structure table through any method\n");
        return FALSE;
    }

    return TRUE;
}
