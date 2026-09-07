#include "general.h"
#include "patch.h"
#include "smbios.h"
#include "utils.h"
#include "editme.h"
#include "log.h"

BOOLEAN PatchType1(SMBIOS_CONTEXT* ctx)
{
    if (!ctx || !ctx->TableAddress)
    {
        LOG_ERROR(L"Invalid context provided to PatchType1\r\n");
        return FALSE;
    }

    SMBIOS_STRUCTURE_POINTER table =
        FindTableByType(ctx, SMBIOS_TYPE_SYSTEM_INFORMATION, 0);

    if (!table.Raw)
    {
        LOG_ERROR(L"Type 1 (System Information) table not found\r\n");
        return FALSE;
    }

    LOG_INFO(L"Found Type 1 (System Information) at 0x%lx (Length: %u)\r\n",
             (UINT64)(UINTN)table.Raw, table.Hdr->Length);

    BOOLEAN uuidPatched = FALSE;
    if (table.Hdr->Length >= 0x18)
    {
        EFI_GUID* uuidField = (EFI_GUID*)(table.Raw + 0x08);
        CopyMem(uuidField, &SMBIOS_SYS_UUID, sizeof(EFI_GUID));
        LOG_SUCCESS(L"  Patched Type 1 System UUID\r\n");
        uuidPatched = TRUE;
    }
    else
    {
        LOG_WARN(L"  Type 1 table too short for UUID (%u < 0x18)\r\n", table.Hdr->Length);
    }

#ifdef SMBIOS_SYS_SERIAL
    if (table.Hdr->Length > 0x07)
    {
        SMBIOS_STRING* serialField = (SMBIOS_STRING*)(table.Raw + 0x07);
        CHAR8 currentSerial[128];
        if (SmbiosGetString(table, *serialField, currentSerial, sizeof(currentSerial)))
        {
            LOG_INFO(L"  Current System Serial (index %u): \"%a\"\r\n", (UINT32)*serialField, currentSerial);
        }
        else
        {
            LOG_INFO(L"  Current System Serial (index %u): [None / Empty]\r\n", (UINT32)*serialField);
        }

        if (SMBIOS_SYS_SERIAL == NULL)
        {
            if (SmbiosSetString(ctx, table, serialField, NULL))
            {
                LOG_SUCCESS(L"  Set Type 1 System Serial to NULL (index 0, string removed)\r\n");
            }
            else
            {
                LOG_ERROR(L"  Failed to set Type 1 System Serial to NULL\r\n");
            }
        }
        else if (((const char*)SMBIOS_SYS_SERIAL)[0] != '\0')
        {
            if (SmbiosSetString(ctx, table, serialField, SMBIOS_SYS_SERIAL))
            {
                table = FindTableByType(ctx, SMBIOS_TYPE_SYSTEM_INFORMATION, 0);
                if (table.Raw != NULL && table.Hdr->Length > 0x07)
                {
                    serialField = (SMBIOS_STRING*)(table.Raw + 0x07);
                    CHAR8 verifySerial[128];
                    if (SmbiosGetString(table, *serialField, verifySerial, sizeof(verifySerial)))
                    {
                        LOG_SUCCESS(L"  Patched Type 1 System Serial (index %u): \"%a\"\r\n", (UINT32)*serialField, verifySerial);
                    }
                    else
                    {
                        LOG_SUCCESS(L"  Patched Type 1 System Serial (index %u)\r\n", (UINT32)*serialField);
                    }
                }
            }
            else
            {
                LOG_ERROR(L"  Failed to patch Type 1 System Serial\r\n");
            }
        }
    }
#endif

    return uuidPatched;
}

UINTN PatchType17(SMBIOS_CONTEXT* ctx)
{
    if (!ctx || !ctx->TableAddress)
    {
        LOG_ERROR(L"Invalid context provided to PatchType17\r\n");
        return 0;
    }

    if (SMBIOS_MEM_SERIAL != NULL && SMBIOS_MEM_SERIAL[0] == '\0')
    {
        LOG_WARN(L"SMBIOS_MEM_SERIAL is empty, skipping Type 17 patching\r\n");
        return 0;
    }

    UINTN patchedCount = 0;
    UINTN emptySlotCount = 0;
    UINTN index = 0;

    for (;; index++)
    {
        SMBIOS_STRUCTURE_POINTER table =
            FindTableByType(ctx, SMBIOS_TYPE_MEMORY_DEVICE, index);

        if (!table.Raw)
        {
            if (index == 0)
            {
                LOG_ERROR(L"No Type 17 (Memory Device) tables found\r\n");
            }
            break;
        }

        // Offset 0x0C: Size (UINT16). 0 indicates unpopulated socket.
        UINT16 memSize = *(UINT16*)(table.Raw + 0x0C);
        if (memSize == 0)
        {
            LOG_INFO(L"Memory Slot #%u is empty (Size is 0), skipping\r\n", index);
            emptySlotCount++;
            continue;
        }

        LOG_INFO(L"Processing Memory Device #%u at 0x%lx (Length: %u)...\r\n",
                 index, (UINT64)(UINTN)table.Raw, table.Hdr->Length);

        // Serial Number string index is at offset 0x18
        if (table.Hdr->Length <= 0x18)
        {
            LOG_WARN(L"  Type 17 structure too small (%u <= 0x18), skipping slot #%u\r\n",
                     table.Hdr->Length, index);
            continue;
        }

        SMBIOS_STRING* serialField = (SMBIOS_STRING*)(table.Raw + 0x18);

        CHAR8 currentSerial[128];
        if (SmbiosGetString(table, *serialField, currentSerial, sizeof(currentSerial)))
        {
            LOG_INFO(L"  Current Serial (index %u): \"%a\"\r\n", (UINT32)*serialField, currentSerial);
        }
        else
        {
            LOG_INFO(L"  Current Serial (index %u): [None / Empty]\r\n", (UINT32)*serialField);
        }

        if (SmbiosSetString(ctx, table, serialField, SMBIOS_MEM_SERIAL))
        {
            if (SMBIOS_MEM_SERIAL == NULL)
            {
                LOG_SUCCESS(L"  Set Serial to NULL (index 0, string removed)\r\n");
            }
            else
            {
                // Re-resolve table after potential memory shift/reallocation
                table = FindTableByType(ctx, SMBIOS_TYPE_MEMORY_DEVICE, index);
                if (table.Raw != NULL && table.Hdr->Length > 0x18)
                {
                    serialField = (SMBIOS_STRING*)(table.Raw + 0x18);
                    CHAR8 verifySerial[128];
                    if (SmbiosGetString(table, *serialField, verifySerial, sizeof(verifySerial)))
                    {
                        LOG_SUCCESS(L"  Patched Serial (index %u): \"%a\"\r\n", (UINT32)*serialField, verifySerial);
                    }
                    else
                    {
                        LOG_SUCCESS(L"  Patched Serial (index %u)\r\n", (UINT32)*serialField);
                    }
                }
            }
            patchedCount++;
        }
        else
        {
            LOG_ERROR(L"  Failed to patch serial on slot #%u\r\n", index);
        }
    }

    LOG_INFO(L"Type 17 summary: %u slot(s) patched, %u empty slot(s) skipped (total examined: %u)\r\n",
             patchedCount, emptySlotCount, index);

    return patchedCount;
}

BOOLEAN PatchAll(SMBIOS_CONTEXT* ctx)
{
    if (!ctx)
        return FALSE;

    BOOLEAN t1 = PatchType1(ctx);
    UINTN t17 = PatchType17(ctx);

    SmbiosUpdateChecksums(ctx);

    return t1 || (t17 > 0);
}

UINTN GetLogPauseSeconds(VOID)
{
#ifdef LOG_PAUSE_SECONDS
    return LOG_PAUSE_SECONDS;
#else
    return 5;
#endif
}