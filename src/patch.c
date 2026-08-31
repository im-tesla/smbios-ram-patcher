#include "general.h"
#include "patch.h"
#include "smbios.h"
#include "utils.h"
#include "editme.h"

static VOID
EditUserString(
    SMBIOS_STRUCTURE_POINTER table,
    SMBIOS_STRING* field,
    const char* value
)
{
    if (table.Raw && field && value)
    {
        EditString(table, field, value);
    }
}

VOID PatchType1(SMBIOS_STRUCTURE_TABLE* entry)
{
    SMBIOS_STRUCTURE_POINTER table =
        FindTableByType(entry, SMBIOS_TYPE_SYSTEM_INFORMATION, 0);

    Print(L"[WORK] Patching type1 table at 0x%08x...\n", table.Raw);

    if (!table.Type1)
    {
        Print(L"[FAIL] Table type1 is non existent\n");
        return;
    }

    CopyMem(
        &table.Type1->Uuid,
        &SMBIOS_SYS_UUID,
        sizeof(EFI_GUID)
    );

    Print(L"[INFO] Patched type1 table (Serial + UUID)\n");
}


VOID PatchType17(SMBIOS_STRUCTURE_TABLE* entry)
{
    UINTN index = 0;

    for (;; index++)
    {
        SMBIOS_STRUCTURE_POINTER table =
            FindTableByType(entry, SMBIOS_TYPE_MEMORY_DEVICE, index);

        if (!table.Raw)
        {
            if (index == 0)
            {
                Print(L"[FAIL] Table type17 is non existent\n");
            }
            break;
        }

        UINT16* memSize = (UINT16*)(table.Raw + 0x0C);

        if (*memSize == 0) 
        {
            Print(L"[INFO] Slot %d is empty (Size is 0), skipping...\n", index);
            continue; 
        }

        Print(L"[WORK] Patching type17 table at 0x%08x (index %d)...\n", table.Raw, index);

        if (table.Hdr->Length > 0x18)
        {
            SMBIOS_STRING* serialField = (SMBIOS_STRING*)(table.Raw + 0x18);

            EditUserString(
                table,
                serialField,
                SMBIOS_MEM_SERIAL
            );

            Print(L"[INFO] Patched type17 table (index %d)\n", index);
        } else {
            Print(L"[WARN] Type17 table length too small, skipping (index %d)\n", index);
        }
    }
}

VOID PatchAll(SMBIOS_STRUCTURE_TABLE* entry)
{
    PatchType1(entry);
    PatchType17(entry);
}