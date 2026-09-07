#include "general.h"
#include "smbios.h"
#include "log.h"

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

UINT32 TableLength(SMBIOS_STRUCTURE_POINTER table)
{
    if (!table.Raw || table.Hdr->Length < 4)
        return 0;

    const CHAR8* p = (const CHAR8*)(table.Raw + table.Hdr->Length);

    // SMBIOS strings block ends with two null bytes (0x00, 0x00)
    while (!(*p == '\0' && *(p + 1) == '\0'))
    {
        p++;
    }

    return (UINT32)((UINTN)(p + 2) - (UINTN)table.Raw);
}

UINT32 SmbiosGetTotalTableSize(UINT8* tableStart)
{
    if (!tableStart)
        return 0;

    SMBIOS_STRUCTURE_POINTER curr;
    curr.Raw = tableStart;

    while (curr.Hdr->Type != SMBIOS_TYPE_END_OF_TABLE)
    {
        UINT32 len = TableLength(curr);
        if (len == 0)
            break;
        curr.Raw += len;
    }

    // Include the Type 127 structure itself
    if (curr.Hdr->Type == SMBIOS_TYPE_END_OF_TABLE)
    {
        UINT32 len = TableLength(curr);
        if (len > 0)
            curr.Raw += len;
    }

    return (UINT32)((UINTN)curr.Raw - (UINTN)tableStart);
}

SMBIOS_STRUCTURE_POINTER FindTableByType(SMBIOS_CONTEXT* ctx, UINT8 type, UINTN index)
{
    SMBIOS_STRUCTURE_POINTER smbiosTable;
    smbiosTable.Raw = NULL;

    if (!ctx || !ctx->TableAddress)
        return smbiosTable;

    smbiosTable.Raw = ctx->TableAddress;
    UINTN typeIndex = 0;

    while (TRUE)
    {
        if (smbiosTable.Hdr->Type == type)
        {
            if (typeIndex == index)
            {
                return smbiosTable;
            }
            typeIndex++;
        }

        if (smbiosTable.Hdr->Type == SMBIOS_TYPE_END_OF_TABLE)
        {
            smbiosTable.Raw = NULL;
            return smbiosTable;
        }

        UINT32 len = TableLength(smbiosTable);
        if (len == 0)
        {
            smbiosTable.Raw = NULL;
            return smbiosTable;
        }

        smbiosTable.Raw += len;

        if ((UINTN)(smbiosTable.Raw - ctx->TableAddress) >= ctx->TableLength)
        {
            smbiosTable.Raw = NULL;
            return smbiosTable;
        }
    }
}

BOOLEAN SmbiosGetString(
    SMBIOS_STRUCTURE_POINTER table,
    UINT8 stringIndex,
    CHAR8* outBuffer,
    UINTN outBufferSize
)
{
    if (!table.Raw || stringIndex == 0 || !outBuffer || outBufferSize == 0)
    {
        if (outBuffer && outBufferSize > 0)
            outBuffer[0] = '\0';
        return FALSE;
    }

    const CHAR8* str = (const CHAR8*)(table.Raw + table.Hdr->Length);
    UINT8 curIdx = 1;

    if (*str == '\0' && *(str + 1) == '\0')
    {
        outBuffer[0] = '\0';
        return FALSE;
    }

    while (TRUE)
    {
        if (*str == '\0')
        {
            outBuffer[0] = '\0';
            return FALSE;
        }

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
}

BOOLEAN SmbiosEnsureCapacity(SMBIOS_CONTEXT* ctx, UINT32 requiredSize)
{
    if (requiredSize <= ctx->TableMaxAlloc)
        return TRUE;

    UINTN pagesNeeded = EFI_SIZE_TO_PAGES(requiredSize + 4096);
    EFI_PHYSICAL_ADDRESS newAddr = 0;

    EFI_STATUS status = gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesData, pagesNeeded, &newAddr);
    if (EFI_ERROR(status))
    {
        status = gBS->AllocatePages(AllocateAnyPages, EfiACPIMemoryNVS, pagesNeeded, &newAddr);
        if (EFI_ERROR(status))
        {
            status = gBS->AllocatePages(AllocateAnyPages, EfiBootServicesData, pagesNeeded, &newAddr);
            if (EFI_ERROR(status))
            {
                LOG_ERROR(L"Failed to allocate memory for SMBIOS expansion: %r\r\n", status);
                return FALSE;
            }
        }
    }

    VOID* newTable = (VOID*)(UINTN)newAddr;
    CopyMem(newTable, ctx->TableAddress, ctx->TableLength);

    ctx->TableAddress = (UINT8*)newTable;
    ctx->TableMaxAlloc = (UINT32)(pagesNeeded * EFI_PAGE_SIZE);
    ctx->IsDynamicAlloc = TRUE;

    if (ctx->Version == SMBIOS_VERSION_2)
    {
        ((SMBIOS_STRUCTURE_TABLE*)ctx->EntryPoint)->TableAddress = (UINT32)(UINTN)ctx->TableAddress;
    }
    else if (ctx->Version == SMBIOS_VERSION_3)
    {
        ((SMBIOS_3_0_ENTRY_POINT*)ctx->EntryPoint)->TableAddress = (UINT64)(UINTN)ctx->TableAddress;
    }

    SmbiosUpdateChecksums(ctx);
    return TRUE;
}

VOID SmbiosUpdateChecksums(SMBIOS_CONTEXT* ctx)
{
    if (!ctx || !ctx->EntryPoint)
        return;

    if (ctx->Version == SMBIOS_VERSION_2)
    {
        SMBIOS_STRUCTURE_TABLE* ep = (SMBIOS_STRUCTURE_TABLE*)ctx->EntryPoint;
        ep->TableLength = (UINT16)ctx->TableLength;
        ep->TableAddress = (UINT32)(UINTN)ctx->TableAddress;

        // Intermediate checksum covers offset 0x10 to 0x1F (16 bytes)
        ep->IntermediateChecksum = 0;
        UINT8 sum = 0;
        UINT8* p = (UINT8*)ep + 0x10;
        for (UINTN i = 0; i < 0x10; i++)
        {
            sum = (UINT8)(sum + p[i]);
        }
        ep->IntermediateChecksum = (UINT8)(0 - sum);

        // Structure checksum covers full EntryPointLength
        ep->EntryPointStructureChecksum = 0;
        sum = 0;
        p = (UINT8*)ep;
        for (UINTN i = 0; i < ep->EntryPointLength; i++)
        {
            sum = (UINT8)(sum + p[i]);
        }
        ep->EntryPointStructureChecksum = (UINT8)(0 - sum);
    }
    else if (ctx->Version == SMBIOS_VERSION_3)
    {
        SMBIOS_3_0_ENTRY_POINT* ep3 = (SMBIOS_3_0_ENTRY_POINT*)ctx->EntryPoint;
        if (ctx->TableLength > ep3->TableMaximumSize)
        {
            ep3->TableMaximumSize = ctx->TableLength;
        }
        ep3->TableAddress = (UINT64)(UINTN)ctx->TableAddress;

        ep3->EntryPointStructureChecksum = 0;
        UINT8 sum = 0;
        UINT8* p = (UINT8*)ep3;
        for (UINTN i = 0; i < ep3->EntryPointLength; i++)
        {
            sum = (UINT8)(sum + p[i]);
        }
        ep3->EntryPointStructureChecksum = (UINT8)(0 - sum);
    }
}

static VOID SmbiosFixupStringFields(SMBIOS_STRUCTURE_POINTER table, UINT8 removedIdx)
{
    UINT8 type = table.Hdr->Type;
    UINT8 len = table.Hdr->Length;

    if (type == SMBIOS_TYPE_SYSTEM_INFORMATION)
    {
        const UINT8 offsets[] = { 0x04, 0x05, 0x06, 0x07, 0x19, 0x1A };
        for (UINTN i = 0; i < sizeof(offsets); i++)
        {
            if (offsets[i] < len)
            {
                UINT8* f = table.Raw + offsets[i];
                if (*f > removedIdx)
                {
                    (*f)--;
                }
            }
        }
    }
    else if (type == SMBIOS_TYPE_MEMORY_DEVICE)
    {
        const UINT8 offsets[] = { 0x10, 0x11, 0x17, 0x18, 0x19, 0x1A };
        for (UINTN i = 0; i < sizeof(offsets); i++)
        {
            if (offsets[i] < len)
            {
                UINT8* f = table.Raw + offsets[i];
                if (*f > removedIdx)
                {
                    (*f)--;
                }
            }
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

    if (*p == '\0' && *(p + 1) == '\0')
    {
        *field = 0;
        return TRUE;
    }

    UINT8 stringCount = 0;
    CHAR8* targetStr = NULL;
    UINT8 curIdx = 1;

    while (*p != '\0')
    {
        if (curIdx == targetIdx)
        {
            targetStr = p;
        }
        stringCount++;
        while (*p != '\0')
            p++;
        p++;
        curIdx++;
    }

    if (!targetStr)
    {
        *field = 0;
        return TRUE;
    }

    UINTN oldLen = 0;
    while (targetStr[oldLen] != '\0')
        oldLen++;

    UINT32 bytesToRemove = 0;
    UINT8* moveSrc = NULL;
    UINT8* moveDst = (UINT8*)targetStr;

    if (stringCount == 1)
    {
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

    if (stringCount == 1)
    {
        table.Raw[table.Hdr->Length] = '\0';
        table.Raw[table.Hdr->Length + 1] = '\0';
    }

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
    {
        return SmbiosRemoveString(ctx, table, field);
    }

    UINTN newLen = 0;
    while (newStr[newLen] != '\0')
        newLen++;

    CHAR8* existingStr = NULL;
    UINTN oldLen = 0;

    if (*field != 0)
    {
        CHAR8* p = (CHAR8*)(table.Raw + table.Hdr->Length);
        UINT8 curIdx = 1;

        if (!(*p == '\0' && *(p + 1) == '\0'))
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

    if (existingStr != NULL && oldLen > 0)
    {
        if (newLen <= oldLen)
        {
            CopyMem(existingStr, (VOID*)newStr, newLen);
            for (UINTN i = newLen; i < oldLen; i++)
            {
                existingStr[i] = ' ';
            }
            existingStr[oldLen] = '\0';
            return TRUE;
        }
        else
        {
            INTN diff = (INTN)(newLen - oldLen);
            UINTN strOffset = (UINTN)((UINT8*)existingStr - ctx->TableAddress);
            UINTN fieldOffset = (UINTN)((UINT8*)field - ctx->TableAddress);

            UINT8* moveSrc = (UINT8*)existingStr + oldLen;
            UINTN bytesToMove = (UINTN)((ctx->TableAddress + ctx->TableLength) - moveSrc);

            if (!SmbiosEnsureCapacity(ctx, ctx->TableLength + (UINT32)diff))
            {
                LOG_ERROR(L"Cannot expand SMBIOS table: out of memory\r\n");
                return FALSE;
            }

            existingStr = (CHAR8*)(ctx->TableAddress + strOffset);
            field = (SMBIOS_STRING*)(ctx->TableAddress + fieldOffset);
            moveSrc = (UINT8*)existingStr + oldLen;
            UINT8* moveDst = moveSrc + diff;

            SafeMoveMem(moveDst, moveSrc, bytesToMove);
            CopyMem(existingStr, (VOID*)newStr, newLen);

            ctx->TableLength += (UINT32)diff;
            SmbiosUpdateChecksums(ctx);
            return TRUE;
        }
    }
    else
    {
        CHAR8* p = (CHAR8*)(table.Raw + table.Hdr->Length);
        UINT8 stringCount = 0;
        UINT32 bytesToAdd = 0;
        UINTN insertOffset = 0;
        UINTN fieldOffset = (UINTN)((UINT8*)field - ctx->TableAddress);
        UINT8 newIndex = 1;

        if (*p == '\0' && *(p + 1) == '\0')
        {
            insertOffset = (UINTN)((UINT8*)p - ctx->TableAddress);
            bytesToAdd = (UINT32)newLen;
            newIndex = 1;

            UINT8* moveSrc = (UINT8*)p + 1;
            UINTN bytesToMove = (UINTN)((ctx->TableAddress + ctx->TableLength) - moveSrc);

            if (!SmbiosEnsureCapacity(ctx, ctx->TableLength + bytesToAdd))
            {
                LOG_ERROR(L"Cannot expand SMBIOS table: out of memory\r\n");
                return FALSE;
            }

            UINT8* insertDst = ctx->TableAddress + insertOffset;
            field = (SMBIOS_STRING*)(ctx->TableAddress + fieldOffset);
            moveSrc = insertDst + 1;
            UINT8* moveDst = moveSrc + bytesToAdd;

            SafeMoveMem(moveDst, moveSrc, bytesToMove);
            CopyMem(insertDst, (VOID*)newStr, newLen);
            insertDst[newLen] = '\0';

            *field = newIndex;
            ctx->TableLength += bytesToAdd;
            SmbiosUpdateChecksums(ctx);
            return TRUE;
        }
        else
        {
            while (*p != '\0')
            {
                stringCount++;
                while (*p != '\0')
                    p++;
                p++;
            }
            insertOffset = (UINTN)((UINT8*)p - ctx->TableAddress);
            bytesToAdd = (UINT32)(newLen + 1);
            newIndex = (UINT8)(stringCount + 1);

            UINT8* moveSrc = (UINT8*)p;
            UINTN bytesToMove = (UINTN)((ctx->TableAddress + ctx->TableLength) - moveSrc);

            if (!SmbiosEnsureCapacity(ctx, ctx->TableLength + bytesToAdd))
            {
                LOG_ERROR(L"Cannot expand SMBIOS table: out of memory\r\n");
                return FALSE;
            }

            UINT8* insertDst = ctx->TableAddress + insertOffset;
            field = (SMBIOS_STRING*)(ctx->TableAddress + fieldOffset);
            moveSrc = insertDst;
            UINT8* moveDst = moveSrc + bytesToAdd;

            SafeMoveMem(moveDst, moveSrc, bytesToMove);
            CopyMem(insertDst, (VOID*)newStr, newLen);
            insertDst[newLen] = '\0';

            *field = newIndex;
            ctx->TableLength += bytesToAdd;
            SmbiosUpdateChecksums(ctx);
            return TRUE;
        }
    }
}

UINTN SpaceLength(const char* text, UINTN maxLength)
{
    if (!text)
        return 0;

    UINTN len = 0;
    if (maxLength > 0)
    {
        for (len = 0; len < maxLength; len++)
        {
            if (text[len] == '\0')
                break;
        }
        while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\0'))
        {
            len--;
        }
    }
    else
    {
        while (text[len] != '\0')
            len++;
    }
    return len;
}

void EditString(SMBIOS_STRUCTURE_POINTER table, SMBIOS_STRING* field, const char* buffer)
{
    // Legacy fallback stub for backward compatibility
    if (!table.Raw || !buffer || !field)
        return;

    UINTN bLen = 0;
    while (buffer[bLen] != '\0')
        bLen++;

    CHAR8* p = (CHAR8*)(table.Raw + table.Hdr->Length);
    UINT8 cur = 1;
    while (*p != '\0')
    {
        if (cur == *field)
        {
            UINTN oldLen = 0;
            while (p[oldLen] != '\0')
                oldLen++;
            if (bLen <= oldLen)
            {
                CopyMem(p, (VOID*)buffer, bLen);
                for (UINTN i = bLen; i < oldLen; i++)
                    p[i] = ' ';
            }
            return;
        }
        while (*p != '\0')
            p++;
        p++;
        cur++;
    }
}