#include "general.h"
#include "smbios.h"
#include "log.h"

UINT16 SmbiosGetHandle(SMBIOS_STRUCTURE_POINTER table)
{
    // Handle is declared as UINT8[2] because SMBIOS structures are not aligned.
    if (!table.Raw)
        return 0;

    return (UINT16)(table.Hdr->Handle[0] | ((UINT16)table.Hdr->Handle[1] << 8));
}

VOID SafeMoveMem(VOID* dest, CONST VOID* src, UINTN count)
{
    UINT8* d = (UINT8*)dest;
    CONST UINT8* s = (CONST UINT8*)src;

    if (d == s || count == 0)
        return;

    if (d < s)
    {
        for (UINTN i = 0; i < count; i++)
        {
            d[i] = s[i];
        }
    }
    else
    {
        for (UINTN i = count; i > 0; i--)
        {
            d[i - 1] = s[i - 1];
        }
    }
}

UINT32 TableLength(SMBIOS_STRUCTURE_POINTER table, CONST UINT8* limit)
{
    if (!table.Raw || table.Hdr->Length < 4)
        return 0;

    CONST UINT8* p = table.Raw + table.Hdr->Length;

    // `limit` is one past the last readable byte.
    if (limit == NULL || limit > table.Raw + SMBIOS_MAX_TABLE_SIZE)
        limit = table.Raw + SMBIOS_MAX_TABLE_SIZE;

    // The string area of a structure ends with two consecutive NUL bytes.
    while (p + 2 <= limit)
    {
        if (p[0] == '\0' && p[1] == '\0')
            return (UINT32)((UINTN)(p + 2) - (UINTN)table.Raw);
        p++;
    }

    return 0;
}

UINT32 SmbiosGetTotalTableSize(UINT8* tableStart, UINT32 maxScan, UINT16* outCount)
{
    if (outCount != NULL)
        *outCount = 0;

    if (!tableStart || maxScan < 4)
        return 0;

    if (maxScan > SMBIOS_MAX_TABLE_SIZE)
        maxScan = SMBIOS_MAX_TABLE_SIZE;

    CONST UINT8* end = tableStart + maxScan;
    SMBIOS_STRUCTURE_POINTER curr;
    UINT16 count = 0;

    curr.Raw = tableStart;

    while (curr.Raw + sizeof(SMBIOS_HEADER) <= end)
    {
        UINT8  type = curr.Hdr->Type;
        UINT32 len = TableLength(curr, end);

        if (len == 0)
            return 0;

        count++;
        curr.Raw += len;

        if (type == SMBIOS_TYPE_END_OF_TABLE)
        {
            if (outCount != NULL)
                *outCount = count;
            return (UINT32)((UINTN)curr.Raw - (UINTN)tableStart);
        }
    }

    return 0;
}

SMBIOS_STRUCTURE_POINTER FindTableByType(SMBIOS_CONTEXT* ctx, UINT8 type, UINTN index)
{
    SMBIOS_STRUCTURE_POINTER smbiosTable;
    smbiosTable.Raw = NULL;

    if (!ctx || !ctx->TableAddress || ctx->TableLength == 0)
        return smbiosTable;

    CONST UINT8* end = ctx->TableAddress + ctx->TableLength;
    UINTN typeIndex = 0;

    smbiosTable.Raw = ctx->TableAddress;

    while (smbiosTable.Raw + sizeof(SMBIOS_HEADER) <= end)
    {
        if (smbiosTable.Hdr->Type == type)
        {
            if (typeIndex == index)
                return smbiosTable;

            typeIndex++;
        }

        if (smbiosTable.Hdr->Type == SMBIOS_TYPE_END_OF_TABLE)
            break;

        UINT32 len = TableLength(smbiosTable, end);
        if (len == 0)
            break;

        smbiosTable.Raw += len;
    }

    smbiosTable.Raw = NULL;
    return smbiosTable;
}

BOOLEAN SmbiosGetString(
    SMBIOS_STRUCTURE_POINTER table,
    UINT8 stringIndex,
    CHAR8* outBuffer,
    UINTN outBufferSize
)
{
    if (outBuffer != NULL && outBufferSize > 0)
        outBuffer[0] = '\0';

    if (!table.Raw || stringIndex == 0 || !outBuffer || outBufferSize == 0)
        return FALSE;

    const CHAR8* str = (const CHAR8*)(table.Raw + table.Hdr->Length);
    UINT8 curIdx = 1;

    // No strings at all.
    if (str[0] == '\0' && str[1] == '\0')
        return FALSE;

    while (*str != '\0')
    {
        if (curIdx == stringIndex)
        {
            UINTN i = 0;
            while (str[i] != '\0' && (i + 1) < outBufferSize)
            {
                outBuffer[i] = str[i];
                i++;
            }
            outBuffer[i] = '\0';
            return TRUE;
        }

        while (*str != '\0')
            str++;
        str++;
        curIdx++;
    }

    return FALSE;
}

// Recomputes the structure count and the largest structure size of the
// currently active table.
static VOID SmbiosScanGeometry(SMBIOS_CONTEXT* ctx, UINT16* outCount, UINT16* outMaxStruct)
{
    UINT16 count = 0;
    UINT32 maxStruct = 0;

    CONST UINT8* end = ctx->TableAddress + ctx->TableLength;
    SMBIOS_STRUCTURE_POINTER curr;
    curr.Raw = ctx->TableAddress;

    while (curr.Raw + sizeof(SMBIOS_HEADER) <= end)
    {
        UINT8  type = curr.Hdr->Type;
        UINT32 len = TableLength(curr, end);

        if (len == 0)
            break;

        count++;
        if (len > maxStruct)
            maxStruct = len;

        curr.Raw += len;

        if (type == SMBIOS_TYPE_END_OF_TABLE)
            break;
    }

    if (outCount != NULL)
        *outCount = count;

    if (outMaxStruct != NULL)
        *outMaxStruct = (maxStruct > 0xFFFF) ? 0xFFFF : (UINT16)maxStruct;
}

BOOLEAN SmbiosEnsureCapacity(SMBIOS_CONTEXT* ctx, UINT32 requiredSize)
{
    if (!ctx || !ctx->TableAddress)
        return FALSE;

    if (requiredSize <= ctx->TableMaxAlloc)
        return TRUE;

    // The firmware buffer has no guaranteed headroom, so any growth means
    // moving the table into memory we own. Reserve a page of slack so that
    // subsequent edits stay in place.
    UINTN pagesNeeded = EFI_SIZE_TO_PAGES(requiredSize + EFI_PAGE_SIZE);
    EFI_PHYSICAL_ADDRESS newAddr = 0;
    EFI_STATUS status;

    if (ctx->EntryPoint2 != NULL)
    {
        // A SMBIOS 2.x entry point stores the table address in a UINT32, so
        // the replacement buffer has to live below 4 GB.
        newAddr = 0xFFFFFFFF;
        status = gBS->AllocatePages(AllocateMaxAddress, EfiRuntimeServicesData, pagesNeeded, &newAddr);
        if (EFI_ERROR(status))
        {
            newAddr = 0xFFFFFFFF;
            status = gBS->AllocatePages(AllocateMaxAddress, EfiACPIMemoryNVS, pagesNeeded, &newAddr);
        }
    }
    else
    {
        status = gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesData, pagesNeeded, &newAddr);
        if (EFI_ERROR(status))
        {
            status = gBS->AllocatePages(AllocateAnyPages, EfiACPIMemoryNVS, pagesNeeded, &newAddr);
        }
    }

    if (EFI_ERROR(status) || newAddr == 0)
    {
        LOG_ERROR(L"Failed to allocate %u page(s) for SMBIOS table expansion: %r\n",
                  (UINT32)pagesNeeded, status);
        return FALSE;
    }

    UINT8* newTable = (UINT8*)(UINTN)newAddr;

    ZeroMem(newTable, pagesNeeded * EFI_PAGE_SIZE);
    CopyMem(newTable, ctx->TableAddress, ctx->TableLength);

    LOG_INFO(L"Relocated SMBIOS structure table 0x%lx -> 0x%lx (%u bytes, capacity %u)\n",
             (UINT64)(UINTN)ctx->TableAddress,
             (UINT64)(UINTN)newTable,
             ctx->TableLength,
             (UINT32)(pagesNeeded * EFI_PAGE_SIZE));

    ctx->TableAddress = newTable;
    ctx->TableMaxAlloc = (UINT32)(pagesNeeded * EFI_PAGE_SIZE);
    ctx->IsDynamicAlloc = TRUE;

    SmbiosUpdateChecksums(ctx);
    return TRUE;
}

VOID SmbiosUpdateChecksums(SMBIOS_CONTEXT* ctx)
{
    if (!ctx || !ctx->TableAddress)
        return;

    UINT16 count = 0;
    UINT16 maxStruct = 0;
    SmbiosScanGeometry(ctx, &count, &maxStruct);

    if (count > 0)
        ctx->NumStructures = count;

    if (ctx->EntryPoint3 != NULL)
    {
        SMBIOS_3_0_ENTRY_POINT* ep3 = ctx->EntryPoint3;

        ep3->TableAddress = (UINT64)(UINTN)ctx->TableAddress;
        ep3->TableMaximumSize = ctx->TableLength;

        ep3->EntryPointStructureChecksum = 0;
        UINT8 sum = 0;
        UINT8* p = (UINT8*)ep3;
        for (UINTN i = 0; i < ep3->EntryPointLength; i++)
            sum = (UINT8)(sum + p[i]);
        ep3->EntryPointStructureChecksum = (UINT8)(0 - sum);
    }

    if (ctx->EntryPoint2 != NULL)
    {
        SMBIOS_STRUCTURE_TABLE* ep = ctx->EntryPoint2;

        ep->TableAddress = (UINT32)(UINTN)ctx->TableAddress;
        ep->TableLength = (ctx->TableLength > 0xFFFF) ? 0xFFFF : (UINT16)ctx->TableLength;

        if (count > 0)
            ep->NumberOfSmbiosStructures = count;
        if (maxStruct > 0)
            ep->MaxStructureSize = maxStruct;

        // Intermediate checksum covers the 16 bytes at offset 0x10.
        ep->IntermediateChecksum = 0;
        UINT8 sum = 0;
        UINT8* p = (UINT8*)ep + 0x10;
        for (UINTN i = 0; i < 0x10; i++)
            sum = (UINT8)(sum + p[i]);
        ep->IntermediateChecksum = (UINT8)(0 - sum);

        // Structure checksum covers the whole entry point.
        ep->EntryPointStructureChecksum = 0;
        sum = 0;
        p = (UINT8*)ep;
        for (UINTN i = 0; i < ep->EntryPointLength; i++)
            sum = (UINT8)(sum + p[i]);
        ep->EntryPointStructureChecksum = (UINT8)(0 - sum);
    }
}

// After a string is removed, every string index in the same structure that
// pointed past it has to be shifted down by one.
static VOID SmbiosFixupStringFields(SMBIOS_STRUCTURE_POINTER table, UINT8 removedIdx)
{
    static const UINT8 type1Offsets[] = {
        SMBIOS_T1_MANUFACTURER, SMBIOS_T1_PRODUCT_NAME, SMBIOS_T1_VERSION,
        SMBIOS_T1_SERIAL, SMBIOS_T1_SKU, SMBIOS_T1_FAMILY
    };
    static const UINT8 type17Offsets[] = {
        SMBIOS_T17_DEVICE_LOCATOR, SMBIOS_T17_BANK_LOCATOR, SMBIOS_T17_MANUFACTURER,
        SMBIOS_T17_SERIAL, SMBIOS_T17_ASSET_TAG, SMBIOS_T17_PART_NUMBER
    };

    const UINT8* offsets = NULL;
    UINTN offsetCount = 0;

    if (table.Hdr->Type == SMBIOS_TYPE_SYSTEM_INFORMATION)
    {
        offsets = type1Offsets;
        offsetCount = sizeof(type1Offsets);
    }
    else if (table.Hdr->Type == SMBIOS_TYPE_MEMORY_DEVICE)
    {
        offsets = type17Offsets;
        offsetCount = sizeof(type17Offsets);
    }
    else
    {
        return;
    }

    for (UINTN i = 0; i < offsetCount; i++)
    {
        if (offsets[i] < table.Hdr->Length)
        {
            UINT8* f = table.Raw + offsets[i];
            if (*f > removedIdx)
                (*f)--;
        }
    }
}

BOOLEAN SmbiosRemoveString(
    SMBIOS_CONTEXT* ctx,
    SMBIOS_STRUCTURE_POINTER table,
    SMBIOS_STRING* field
)
{
    if (!ctx || !ctx->TableAddress || !table.Raw || !field)
        return FALSE;

    if (*field == 0)
        return TRUE;

    UINT8 targetIdx = *field;
    CHAR8* p = (CHAR8*)(table.Raw + table.Hdr->Length);

    if (p[0] == '\0' && p[1] == '\0')
    {
        *field = 0;
        return TRUE;
    }

    UINT8  stringCount = 0;
    CHAR8* targetStr = NULL;
    UINT8  curIdx = 1;

    while (*p != '\0')
    {
        if (curIdx == targetIdx)
            targetStr = p;

        stringCount++;
        while (*p != '\0')
            p++;
        p++;
        curIdx++;
    }

    if (targetStr == NULL)
    {
        *field = 0;
        return TRUE;
    }

    UINTN oldLen = 0;
    while (targetStr[oldLen] != '\0')
        oldLen++;

    UINT32 bytesToRemove;
    UINT8* moveSrc;
    UINT8* moveDst = (UINT8*)targetStr;

    if (stringCount == 1)
    {
        // Last string in the structure: the area collapses to the terminating
        // double NUL, so only the characters themselves go away.
        bytesToRemove = (UINT32)oldLen;
        moveSrc = (UINT8*)targetStr + oldLen;
    }
    else
    {
        bytesToRemove = (UINT32)(oldLen + 1);
        moveSrc = (UINT8*)targetStr + oldLen + 1;
    }

    UINTN bytesToMove = (UINTN)((ctx->TableAddress + ctx->TableLength) - moveSrc);
    SafeMoveMem(moveDst, moveSrc, bytesToMove);

    SmbiosFixupStringFields(table, targetIdx);
    *field = 0;
    ctx->TableLength -= bytesToRemove;
    SmbiosUpdateChecksums(ctx);

    return TRUE;
}

BOOLEAN SmbiosSetString(
    SMBIOS_CONTEXT* ctx,
    SMBIOS_STRUCTURE_POINTER table,
    SMBIOS_STRING* field,
    const char* newStr
)
{
    if (!ctx || !ctx->TableAddress || !table.Raw || !field)
        return FALSE;

    if (newStr == NULL)
        return SmbiosRemoveString(ctx, table, field);

    UINTN newLen = 0;
    while (newStr[newLen] != '\0')
        newLen++;

    if (newLen == 0)
        return SmbiosRemoveString(ctx, table, field);

    // Locate the existing string this field points at, if any.
    CHAR8* existingStr = NULL;
    UINTN  oldLen = 0;

    if (*field != 0)
    {
        CHAR8* p = (CHAR8*)(table.Raw + table.Hdr->Length);
        UINT8 curIdx = 1;

        if (!(p[0] == '\0' && p[1] == '\0'))
        {
            while (*p != '\0')
            {
                if (curIdx == *field)
                {
                    existingStr = p;
                    while (p[oldLen] != '\0')
                        oldLen++;
                    break;
                }
                while (*p != '\0')
                    p++;
                p++;
                curIdx++;
            }
        }
    }

    UINTN fieldOffset = (UINTN)((UINT8*)field - ctx->TableAddress);

    if (existingStr != NULL && oldLen > 0)
    {
        UINTN strOffset = (UINTN)((UINT8*)existingStr - ctx->TableAddress);

        if (newLen == oldLen)
        {
            CopyMem(existingStr, (VOID*)newStr, newLen);
            SmbiosUpdateChecksums(ctx);
            return TRUE;
        }

        if (newLen < oldLen)
        {
            UINT32 diff = (UINT32)(oldLen - newLen);
            UINT8* moveSrc = (UINT8*)existingStr + oldLen;
            UINT8* moveDst = (UINT8*)existingStr + newLen;
            UINTN  bytesToMove = (UINTN)((ctx->TableAddress + ctx->TableLength) - moveSrc);

            SafeMoveMem(moveDst, moveSrc, bytesToMove);
            CopyMem(existingStr, (VOID*)newStr, newLen);

            ctx->TableLength -= diff;
            SmbiosUpdateChecksums(ctx);
            return TRUE;
        }

        UINT32 diff = (UINT32)(newLen - oldLen);
        UINTN  bytesToMove = (UINTN)((ctx->TableAddress + ctx->TableLength) -
                                     ((UINT8*)existingStr + oldLen));

        if (!SmbiosEnsureCapacity(ctx, ctx->TableLength + diff))
            return FALSE;

        // The table may have moved; rebuild every pointer from its offset.
        existingStr = (CHAR8*)(ctx->TableAddress + strOffset);
        field = (SMBIOS_STRING*)(ctx->TableAddress + fieldOffset);

        UINT8* moveSrc = (UINT8*)existingStr + oldLen;
        SafeMoveMem(moveSrc + diff, moveSrc, bytesToMove);
        CopyMem(existingStr, (VOID*)newStr, newLen);

        ctx->TableLength += diff;
        SmbiosUpdateChecksums(ctx);
        return TRUE;
    }

    // No string attached to this field yet: append a new one.
    CHAR8* p = (CHAR8*)(table.Raw + table.Hdr->Length);

    if (p[0] == '\0' && p[1] == '\0')
    {
        // The structure has no strings at all. "\0\0" becomes "str\0\0", so
        // the structure grows by exactly newLen bytes.
        UINTN  insertOffset = (UINTN)((UINT8*)p - ctx->TableAddress);
        UINT32 bytesToAdd = (UINT32)newLen;
        UINTN  bytesToMove = (UINTN)((ctx->TableAddress + ctx->TableLength) - ((UINT8*)p + 1));

        if (!SmbiosEnsureCapacity(ctx, ctx->TableLength + bytesToAdd))
            return FALSE;

        UINT8* insertDst = ctx->TableAddress + insertOffset;
        field = (SMBIOS_STRING*)(ctx->TableAddress + fieldOffset);

        SafeMoveMem(insertDst + 1 + bytesToAdd, insertDst + 1, bytesToMove);
        CopyMem(insertDst, (VOID*)newStr, newLen);
        insertDst[newLen] = '\0';

        *field = 1;
        ctx->TableLength += bytesToAdd;
        SmbiosUpdateChecksums(ctx);
        return TRUE;
    }

    // Append after the last existing string.
    UINT8 stringCount = 0;
    while (*p != '\0')
    {
        stringCount++;
        while (*p != '\0')
            p++;
        p++;
    }

    UINTN  insertOffset = (UINTN)((UINT8*)p - ctx->TableAddress);
    UINT32 bytesToAdd = (UINT32)(newLen + 1);
    UINTN  bytesToMove = (UINTN)((ctx->TableAddress + ctx->TableLength) - (UINT8*)p);

    if (!SmbiosEnsureCapacity(ctx, ctx->TableLength + bytesToAdd))
        return FALSE;

    UINT8* insertDst = ctx->TableAddress + insertOffset;
    field = (SMBIOS_STRING*)(ctx->TableAddress + fieldOffset);

    SafeMoveMem(insertDst + bytesToAdd, insertDst, bytesToMove);
    CopyMem(insertDst, (VOID*)newStr, newLen);
    insertDst[newLen] = '\0';

    *field = (UINT8)(stringCount + 1);
    ctx->TableLength += bytesToAdd;
    SmbiosUpdateChecksums(ctx);
    return TRUE;
}
