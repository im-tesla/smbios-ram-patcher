#include "general.h"
#include "finder.h"
#include "smbios.h"
#include "edk2/PiHob.h"
#include "hob.h"
#include "log.h"

EFI_GUID SmbiosTableGuid = { 0xEB9D2D31, 0x2D88, 0x11D3, { 0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D } };
EFI_GUID Smbios3TableGuid = { 0xF2FD1544, 0x9794, 0x4A2C, { 0x99, 0x2E, 0xE5, 0xBB, 0xCF, 0x20, 0xE3, 0x94 } };

#define GET_GUID_HOB_DATA(GuidHob) ((VOID*) (((UINT8*) &((GuidHob)->Name)) + sizeof (EFI_GUID)))

int CheckEntry(SMBIOS_STRUCTURE_TABLE* entry)
{
    if (!entry || entry->EntryPointLength < 0x1F)
        return 0;

    const UINT8* p = (const UINT8*)entry;
    UINT8 sum = 0;
    for (UINTN i = 0; i < entry->EntryPointLength; i++)
    {
        sum = (UINT8)(sum + p[i]);
    }
    if (sum != 0)
        return 0;

    if (CompareMem(entry->IntermediateAnchorString, "_DMI_", 5) != 0)
        return 0;

    sum = 0;
    const UINT8* ip = (const UINT8*)entry + 0x10;
    for (UINTN i = 0; i < 0x10; i++)
    {
        sum = (UINT8)(sum + ip[i]);
    }

    return (sum == 0);
}

static int CheckEntry3(SMBIOS_3_0_ENTRY_POINT* entry)
{
    if (!entry || entry->EntryPointLength < 0x18)
        return 0;

    if (CompareMem(entry->AnchorString, "_SM3_", 5) != 0)
        return 0;

    const UINT8* p = (const UINT8*)entry;
    UINT8 sum = 0;
    for (UINTN i = 0; i < entry->EntryPointLength; i++)
    {
        sum = (UINT8)(sum + p[i]);
    }

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
            if (CheckEntry((SMBIOS_STRUCTURE_TABLE*)offset))
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
        {
            return (VOID*)(UINTN)(*table);
        }
    }

    guidHob.Raw = GetFirstGuidHob(&SmbiosTableGuid);
    if (guidHob.Raw != NULL)
    {
        EFI_PHYSICAL_ADDRESS* table = (EFI_PHYSICAL_ADDRESS*)GET_GUID_HOB_DATA(guidHob.Guid);
        if (table != NULL && *table != 0)
        {
            return (VOID*)(UINTN)(*table);
        }
    }

    return NULL;
}

void* FindByConfig(VOID)
{
    VOID* table = NULL;
    EFI_STATUS status = LibGetSystemConfigurationTable(&Smbios3TableGuid, &table);
    if (status == EFI_SUCCESS && table != NULL)
        return table;

    status = LibGetSystemConfigurationTable(&SmbiosTableGuid, &table);
    if (status == EFI_SUCCESS && table != NULL)
        return table;

    return NULL;
}

SMBIOS_STRUCTURE_TABLE* FindEntry(VOID)
{
    VOID* addr = FindByConfig();
    if (addr)
        return (SMBIOS_STRUCTURE_TABLE*)addr;

    addr = FindByHob();
    if (addr)
        return (SMBIOS_STRUCTURE_TABLE*)addr;

    addr = FindBySignature();
    if (addr)
        return (SMBIOS_STRUCTURE_TABLE*)addr;

    return NULL;
}

BOOLEAN FindSmbios(SMBIOS_CONTEXT* ctx)
{
    if (!ctx)
        return FALSE;

    ZeroMem(ctx, sizeof(SMBIOS_CONTEXT));

    VOID* table = NULL;
    EFI_STATUS status = LibGetSystemConfigurationTable(&Smbios3TableGuid, &table);
    if (status == EFI_SUCCESS && table != NULL)
    {
        SMBIOS_3_0_ENTRY_POINT* ep3 = (SMBIOS_3_0_ENTRY_POINT*)table;
        if (CheckEntry3(ep3))
        {
            ctx->Version = SMBIOS_VERSION_3;
            ctx->EntryPoint = ep3;
            ctx->TableAddress = (UINT8*)(UINTN)ep3->TableAddress;
            ctx->TableLength = SmbiosGetTotalTableSize(ctx->TableAddress);
            if (ctx->TableLength == 0 && ep3->TableMaximumSize > 0)
                ctx->TableLength = ep3->TableMaximumSize;
            ctx->TableMaxAlloc = (UINT32)((((UINTN)ctx->TableAddress + ctx->TableLength + 4095) & ~4095) - (UINTN)ctx->TableAddress);
            ctx->NumStructures = 0;
            ctx->IsDynamicAlloc = FALSE;

            LOG_SUCCESS(L"Located SMBIOS 3.0 via Configuration Table\r\n");
            LOG_INFO(L"  Entry Point:   0x%lx (v%u.%u.%u)\r\n", (UINT64)(UINTN)ep3, ep3->MajorVersion, ep3->MinorVersion, ep3->DocRev);
            LOG_INFO(L"  Table Address: 0x%lx (Length: %u bytes)\r\n", (UINT64)(UINTN)ctx->TableAddress, ctx->TableLength);
            return TRUE;
        }
        else
        {
            LOG_WARN(L"Found SMBIOS3 entry in Config Table but checksum/signature verification failed\r\n");
        }
    }

    status = LibGetSystemConfigurationTable(&SmbiosTableGuid, &table);
    if (status == EFI_SUCCESS && table != NULL)
    {
        SMBIOS_STRUCTURE_TABLE* ep2 = (SMBIOS_STRUCTURE_TABLE*)table;
        if (CheckEntry(ep2))
        {
            ctx->Version = SMBIOS_VERSION_2;
            ctx->EntryPoint = ep2;
            ctx->TableAddress = (UINT8*)(UINTN)ep2->TableAddress;
            ctx->TableLength = ep2->TableLength;
            ctx->TableMaxAlloc = (UINT32)((((UINTN)ctx->TableAddress + ctx->TableLength + 4095) & ~4095) - (UINTN)ctx->TableAddress);
            ctx->NumStructures = ep2->NumberOfSmbiosStructures;
            ctx->IsDynamicAlloc = FALSE;

            LOG_SUCCESS(L"Located SMBIOS 2.x via Configuration Table\r\n");
            LOG_INFO(L"  Entry Point:   0x%lx (v%u.%u)\r\n", (UINT64)(UINTN)ep2, ep2->MajorVersion, ep2->MinorVersion);
            LOG_INFO(L"  Table Address: 0x%lx (Length: %u bytes, Structures: %u)\r\n",
                     (UINT64)(UINTN)ctx->TableAddress, ctx->TableLength, ctx->NumStructures);
            return TRUE;
        }
        else
        {
            LOG_WARN(L"Found SMBIOS 2.x entry in Config Table but checksum/signature verification failed\r\n");
        }
    }

    table = FindByHob();
    if (table != NULL)
    {
        if (CompareMem(table, "_SM3_", 5) == 0 && CheckEntry3((SMBIOS_3_0_ENTRY_POINT*)table))
        {
            SMBIOS_3_0_ENTRY_POINT* ep3 = (SMBIOS_3_0_ENTRY_POINT*)table;
            ctx->Version = SMBIOS_VERSION_3;
            ctx->EntryPoint = ep3;
            ctx->TableAddress = (UINT8*)(UINTN)ep3->TableAddress;
            ctx->TableLength = SmbiosGetTotalTableSize(ctx->TableAddress);
            ctx->TableMaxAlloc = (UINT32)((((UINTN)ctx->TableAddress + ctx->TableLength + 4095) & ~4095) - (UINTN)ctx->TableAddress);
            LOG_SUCCESS(L"Located SMBIOS 3.0 via HOB list\r\n");
            return TRUE;
        }
        else if (CompareMem(table, "_SM_", 4) == 0 && CheckEntry((SMBIOS_STRUCTURE_TABLE*)table))
        {
            SMBIOS_STRUCTURE_TABLE* ep2 = (SMBIOS_STRUCTURE_TABLE*)table;
            ctx->Version = SMBIOS_VERSION_2;
            ctx->EntryPoint = ep2;
            ctx->TableAddress = (UINT8*)(UINTN)ep2->TableAddress;
            ctx->TableLength = ep2->TableLength;
            ctx->TableMaxAlloc = (UINT32)((((UINTN)ctx->TableAddress + ctx->TableLength + 4095) & ~4095) - (UINTN)ctx->TableAddress);
            ctx->NumStructures = ep2->NumberOfSmbiosStructures;
            LOG_SUCCESS(L"Located SMBIOS 2.x via HOB list\r\n");
            return TRUE;
        }
        else
        {
            // Raw structure table without entry point
            ctx->Version = SMBIOS_VERSION_UNKNOWN;
            ctx->EntryPoint = NULL;
            ctx->TableAddress = (UINT8*)table;
            ctx->TableLength = SmbiosGetTotalTableSize(ctx->TableAddress);
            ctx->TableMaxAlloc = (UINT32)((((UINTN)ctx->TableAddress + ctx->TableLength + 4095) & ~4095) - (UINTN)ctx->TableAddress);
            LOG_WARN(L"Located raw SMBIOS structure table via HOB without Entry Point\r\n");
            return TRUE;
        }
    }

    table = FindBySignature();
    if (table != NULL)
    {
        if (CompareMem(table, "_SM3_", 5) == 0)
        {
            SMBIOS_3_0_ENTRY_POINT* ep3 = (SMBIOS_3_0_ENTRY_POINT*)table;
            ctx->Version = SMBIOS_VERSION_3;
            ctx->EntryPoint = ep3;
            ctx->TableAddress = (UINT8*)(UINTN)ep3->TableAddress;
            ctx->TableLength = SmbiosGetTotalTableSize(ctx->TableAddress);
            ctx->TableMaxAlloc = (UINT32)((((UINTN)ctx->TableAddress + ctx->TableLength + 4095) & ~4095) - (UINTN)ctx->TableAddress);
            LOG_SUCCESS(L"Located SMBIOS 3.0 via legacy physical scan\r\n");
            return TRUE;
        }
        else
        {
            SMBIOS_STRUCTURE_TABLE* ep2 = (SMBIOS_STRUCTURE_TABLE*)table;
            ctx->Version = SMBIOS_VERSION_2;
            ctx->EntryPoint = ep2;
            ctx->TableAddress = (UINT8*)(UINTN)ep2->TableAddress;
            ctx->TableLength = ep2->TableLength;
            ctx->TableMaxAlloc = (UINT32)((((UINTN)ctx->TableAddress + ctx->TableLength + 4095) & ~4095) - (UINTN)ctx->TableAddress);
            ctx->NumStructures = ep2->NumberOfSmbiosStructures;
            LOG_SUCCESS(L"Located SMBIOS 2.x via legacy physical scan\r\n");
            return TRUE;
        }
    }

    LOG_ERROR(L"Could not locate SMBIOS table through any method\r\n");
    return FALSE;
}