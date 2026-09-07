#include "general.h"
#include "patch.h"
#include "smbios.h"
#include "utils.h"
#include "editme.h"
#include "log.h"

#define SERIAL_BUF_SIZE 64

static BOOLEAN AsciiEqualsIgnoreCase(const char* a, const char* b)
{
    if (a == NULL || b == NULL)
        return FALSE;

    for (UINTN i = 0;; i++)
    {
        char ca = a[i];
        char cb = b[i];

        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');

        if (ca != cb)
            return FALSE;
        if (ca == '\0')
            return TRUE;
    }
}

// Copies `src` into `dst`, optionally appending a decimal suffix.
static VOID BuildSerial(char* dst, UINTN dstSize, const char* src, BOOLEAN appendIndex, UINTN index)
{
    UINTN n = 0;

    while (src[n] != '\0' && n + 1 < dstSize)
    {
        dst[n] = src[n];
        n++;
    }

    if (appendIndex)
    {
        char digits[16];
        UINTN d = 0;

        if (index == 0)
        {
            digits[d++] = '0';
        }
        else
        {
            while (index > 0 && d < sizeof(digits))
            {
                digits[d++] = (char)('0' + (index % 10));
                index /= 10;
            }
        }

        while (d > 0 && n + 1 < dstSize)
            dst[n++] = digits[--d];
    }

    dst[n] = '\0';
}

// Resolves the configured serial into `buf`.
// Returns TRUE when a string should be written (buf holds it), FALSE when the
// serial should be removed instead.
static BOOLEAN ResolveSerial(const char* configured, char* buf, UINTN bufSize,
                             BOOLEAN appendIndex, UINTN index)
{
    if (configured == NULL)
        return FALSE;

    if (AsciiEqualsIgnoreCase(configured, "RANDOM"))
    {
        RandomText(buf, 8);
        return TRUE;
    }

    BuildSerial(buf, bufSize, configured, appendIndex, index);
    return (buf[0] != '\0');
}

static VOID LogUuid(LOG_LEVEL level, CONST CHAR16* label, CONST UINT8* raw)
{
    // Read byte-wise: SMBIOS structures are not aligned.
    LogPrint(level,
             L"%s%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
             label,
             raw[3], raw[2], raw[1], raw[0],
             raw[5], raw[4],
             raw[7], raw[6],
             raw[8], raw[9],
             raw[10], raw[11], raw[12], raw[13], raw[14], raw[15]);
}

// ---------------------------------------------------------------------------
// Type 1 - System Information
// ---------------------------------------------------------------------------

BOOLEAN PatchType1(SMBIOS_CONTEXT* ctx, PATCH_STATS* stats)
{
    SMBIOS_STRUCTURE_POINTER table = FindTableByType(ctx, SMBIOS_TYPE_SYSTEM_INFORMATION, 0);

    if (!table.Raw)
    {
        LOG_WARN(L"No Type 1 (System Information) structure in this table\n");
        return FALSE;
    }

    LOG_INFO(L"Type 1 (System Information) at 0x%lx, handle 0x%04x, header %u bytes\n",
             (UINT64)(UINTN)table.Raw, SmbiosGetHandle(table), table.Hdr->Length);

    CHAR8 buf[128];

    if (table.Hdr->Length > SMBIOS_T1_MANUFACTURER &&
        SmbiosGetString(table, table.Raw[SMBIOS_T1_MANUFACTURER], buf, sizeof(buf)))
        LOG_DEBUG(L"  Manufacturer: \"%a\"\n", buf);

    if (table.Hdr->Length > SMBIOS_T1_PRODUCT_NAME &&
        SmbiosGetString(table, table.Raw[SMBIOS_T1_PRODUCT_NAME], buf, sizeof(buf)))
        LOG_DEBUG(L"  Product name: \"%a\"\n", buf);

    BOOLEAN changed = FALSE;

    // --- UUID (offset 0x08, 16 bytes) ---
    if (!PATCH_SYS_UUID)
    {
        LOG_DEBUG(L"  UUID patching disabled (PATCH_SYS_UUID = 0)\n");
    }
    else if (table.Hdr->Length >= SMBIOS_T1_UUID + 16)
    {
        UINT8* uuidField = table.Raw + SMBIOS_T1_UUID;

        LogUuid(LOG_LEVEL_DEBUG, L"  Current UUID: ", uuidField);

        CopyMem(uuidField, &SMBIOS_SYS_UUID, sizeof(EFI_GUID));

        LogUuid(LOG_LEVEL_SUCCESS, L"  Patched UUID: ", uuidField);
        changed = TRUE;
    }
    else
    {
        LOG_WARN(L"  Type 1 header is only %u bytes, too short to hold a UUID\n",
                 table.Hdr->Length);
        if (stats) stats->Failures++;
    }

    // --- Serial number (offset 0x07, string index) ---
    if (!PATCH_SYS_SERIAL)
    {
        LOG_DEBUG(L"  System serial patching disabled (PATCH_SYS_SERIAL = 0)\n");
    }
    else if (table.Hdr->Length > SMBIOS_T1_SERIAL)
    {
        SMBIOS_STRING* serialField = (SMBIOS_STRING*)(table.Raw + SMBIOS_T1_SERIAL);

        if (SmbiosGetString(table, *serialField, buf, sizeof(buf)))
            LOG_DEBUG(L"  Current system serial: \"%a\" (string %u)\n", buf, *serialField);
        else
            LOG_DEBUG(L"  Current system serial: none\n");

        char serial[SERIAL_BUF_SIZE];
        BOOLEAN write = ResolveSerial(SMBIOS_SYS_SERIAL, serial, sizeof(serial), FALSE, 0);

        if (SmbiosSetString(ctx, table, serialField, write ? serial : NULL))
        {
            if (!write)
            {
                LOG_SUCCESS(L"  Removed the system serial string\n");
            }
            else
            {
                table = FindTableByType(ctx, SMBIOS_TYPE_SYSTEM_INFORMATION, 0);
                serialField = (SMBIOS_STRING*)(table.Raw + SMBIOS_T1_SERIAL);
                SmbiosGetString(table, *serialField, buf, sizeof(buf));
                LOG_SUCCESS(L"  Patched system serial: \"%a\" (string %u)\n", buf, *serialField);
            }
            changed = TRUE;
        }
        else
        {
            LOG_ERROR(L"  Failed to patch the system serial\n");
            if (stats) stats->Failures++;
        }
    }

    if (changed && stats)
        stats->Type1Patched++;

    return changed;
}

// ---------------------------------------------------------------------------
// Type 17 - Memory Device
// ---------------------------------------------------------------------------

static UINT64 MemoryDeviceSizeMb(SMBIOS_STRUCTURE_POINTER table)
{
    UINT16 size = 0;
    UINT32 extSize = 0;

    if (table.Hdr->Length >= SMBIOS_T17_SIZE + 2)
        CopyMem(&size, table.Raw + SMBIOS_T17_SIZE, sizeof(size));

    if (table.Hdr->Length >= SMBIOS_T17_EXTENDED_SIZE + 4)
        CopyMem(&extSize, table.Raw + SMBIOS_T17_EXTENDED_SIZE, sizeof(extSize));

    if (size == 0xFFFF)
        return 0;                       // unknown

    if (size == 0x7FFF)
        return extSize;                 // size lives in the extended field

    if (size == 0)
        return extSize;                 // slot not populated

    // Bit 15 clear = megabytes, set = kilobytes.
    if ((size & 0x8000) != 0)
        return (UINT64)(size & 0x7FFF) / 1024;

    return size;
}

BOOLEAN PatchType17(SMBIOS_CONTEXT* ctx, PATCH_STATS* stats)
{
    UINTN patched = 0;
    UINTN index = 0;
    BOOLEAN sawAny = FALSE;

    if (!PATCH_MEM_SERIAL)
    {
        LOG_DEBUG(L"Memory serial patching disabled (PATCH_MEM_SERIAL = 0)\n");
        return FALSE;
    }

    for (;; index++)
    {
        SMBIOS_STRUCTURE_POINTER table = FindTableByType(ctx, SMBIOS_TYPE_MEMORY_DEVICE, index);

        if (!table.Raw)
            break;

        sawAny = TRUE;

        if (table.Hdr->Length <= SMBIOS_T17_SERIAL)
        {
            LOG_WARN(L"  Slot #%u: header is only %u bytes, no serial field to patch\n",
                     (UINT32)index, table.Hdr->Length);
            if (stats) stats->Type17Skipped++;
            continue;
        }

        UINT16 speed = 0;
        if (table.Hdr->Length >= SMBIOS_T17_SPEED + 2)
            CopyMem(&speed, table.Raw + SMBIOS_T17_SPEED, sizeof(speed));

        UINT64 sizeMb = MemoryDeviceSizeMb(table);

        CHAR8 locator[64] = { 0 };
        CHAR8 bank[64] = { 0 };
        CHAR8 mfg[64] = { 0 };
        CHAR8 part[64] = { 0 };
        CHAR8 serial[128] = { 0 };

        if (table.Hdr->Length > SMBIOS_T17_DEVICE_LOCATOR)
            SmbiosGetString(table, table.Raw[SMBIOS_T17_DEVICE_LOCATOR], locator, sizeof(locator));
        if (table.Hdr->Length > SMBIOS_T17_BANK_LOCATOR)
            SmbiosGetString(table, table.Raw[SMBIOS_T17_BANK_LOCATOR], bank, sizeof(bank));
        if (table.Hdr->Length > SMBIOS_T17_MANUFACTURER)
            SmbiosGetString(table, table.Raw[SMBIOS_T17_MANUFACTURER], mfg, sizeof(mfg));
        if (table.Hdr->Length > SMBIOS_T17_PART_NUMBER)
            SmbiosGetString(table, table.Raw[SMBIOS_T17_PART_NUMBER], part, sizeof(part));

        SMBIOS_STRING* serialField = (SMBIOS_STRING*)(table.Raw + SMBIOS_T17_SERIAL);
        BOOLEAN hasSerial = SmbiosGetString(table, *serialField, serial, sizeof(serial));

        LOG_DEBUG(L"--- Memory device slot #%u (handle 0x%04x, header %u bytes) ---\n",
                  (UINT32)index, SmbiosGetHandle(table), table.Hdr->Length);
        LOG_DEBUG(L"  Locator \"%a\" / bank \"%a\"\n", locator, bank);
        LOG_DEBUG(L"  Manufacturer \"%a\", part \"%a\"\n", mfg, part);
        LOG_DEBUG(L"  Size %lu MB, speed %u MT/s, serial \"%a\"\n", sizeMb, speed, serial);

        BOOLEAN populated = (sizeMb > 0) || (speed > 0) || (mfg[0] != '\0') || (part[0] != '\0');

        if (!populated && !(hasSerial && serial[0] != '\0'))
        {
            LOG_INFO(L"Slot #%u (\"%a\") is empty, leaving it alone\n", (UINT32)index, locator);
            if (stats) stats->Type17Skipped++;
            continue;
        }

        char newSerial[SERIAL_BUF_SIZE];
        BOOLEAN write = ResolveSerial(SMBIOS_MEM_SERIAL, newSerial, sizeof(newSerial),
                                      SMBIOS_MEM_SERIAL_UNIQUE ? TRUE : FALSE, patched);

        if (SmbiosSetString(ctx, table, serialField, write ? newSerial : NULL))
        {
            if (!write)
            {
                LOG_SUCCESS(L"Slot #%u (\"%a\"): serial string removed\n", (UINT32)index, locator);
            }
            else
            {
                // The table may have moved; re-resolve before reading back.
                table = FindTableByType(ctx, SMBIOS_TYPE_MEMORY_DEVICE, index);
                serialField = (SMBIOS_STRING*)(table.Raw + SMBIOS_T17_SERIAL);
                SmbiosGetString(table, *serialField, serial, sizeof(serial));
                LOG_SUCCESS(L"Slot #%u (\"%a\"): serial \"%a\" (string %u)\n",
                            (UINT32)index, locator, serial, *serialField);
            }

            patched++;
            if (stats) stats->Type17Patched++;
        }
        else
        {
            LOG_ERROR(L"Slot #%u (\"%a\"): failed to patch the serial\n", (UINT32)index, locator);
            if (stats) stats->Failures++;
        }
    }

    if (!sawAny)
        LOG_WARN(L"No Type 17 (Memory Device) structures in this table\n");

    return (patched > 0);
}

// ---------------------------------------------------------------------------

BOOLEAN PatchOneContext(SMBIOS_CONTEXT* ctx, PATCH_STATS* stats)
{
    if (!ctx || !ctx->TableAddress)
        return FALSE;

    CONST CHAR16* version = L"unknown";
    if (ctx->Version == SMBIOS_VERSION_3) version = L"3.x (64-bit)";
    else if (ctx->Version == SMBIOS_VERSION_2) version = L"2.x (32-bit)";

    LOG_INFO(L"Patching SMBIOS %s table at 0x%lx (%u bytes, %u structures)\n",
             version, (UINT64)(UINTN)ctx->TableAddress, ctx->TableLength, ctx->NumStructures);

    BOOLEAN t1 = PatchType1(ctx, stats);
    BOOLEAN t17 = PatchType17(ctx, stats);

    SmbiosUpdateChecksums(ctx);

    LOG_DEBUG(L"  Table now %u bytes at 0x%lx%s\n",
              ctx->TableLength, (UINT64)(UINTN)ctx->TableAddress,
              ctx->IsDynamicAlloc ? L" (relocated)" : L"");

    if (ctx->EntryPoint3 != NULL)
        LOG_DEBUG(L"  SMBIOS 3.x entry point 0x%lx checksum 0x%02x\n",
                  (UINT64)(UINTN)ctx->EntryPoint3, ctx->EntryPoint3->EntryPointStructureChecksum);

    if (ctx->EntryPoint2 != NULL)
        LOG_DEBUG(L"  SMBIOS 2.x entry point 0x%lx checksums 0x%02x / 0x%02x\n",
                  (UINT64)(UINTN)ctx->EntryPoint2,
                  ctx->EntryPoint2->IntermediateChecksum,
                  ctx->EntryPoint2->EntryPointStructureChecksum);

    return t1 || t17;
}

BOOLEAN PatchAll(SMBIOS_CONTEXT_LIST* list, PATCH_STATS* stats)
{
    if (!list || list->Count == 0)
        return FALSE;

    BOOLEAN any = FALSE;

    for (UINTN i = 0; i < list->Count; i++)
    {
        LOG_INFO(L"--- SMBIOS instance %u of %u ---\n", (UINT32)(i + 1), (UINT32)list->Count);

        if (PatchOneContext(&list->Contexts[i], stats))
            any = TRUE;
    }

    return any;
}
