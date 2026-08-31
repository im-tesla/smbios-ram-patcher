#include <string.h>
#include "general.h"
#include "smbios.h"

UINT16 TableLenght(SMBIOS_STRUCTURE_POINTER table) 
{
    char* pointer = (char*)(table.Raw + table.Hdr->Length);
    while ((*pointer != 0) || (*(pointer + 1) != 0)) 
    {
        pointer++;
    }

    return (UINT16)((UINTN)pointer - (UINTN)table.Raw + 2);
}

SMBIOS_STRUCTURE_POINTER FindTableByType(SMBIOS_STRUCTURE_TABLE* entry, UINT8 type, UINTN index) 
{
    SMBIOS_STRUCTURE_POINTER smbiosTable;
    smbiosTable.Raw = (UINT8*)((UINTN)entry->TableAddress);
    if (!smbiosTable.Raw)
        return smbiosTable;

    UINTN typeIndex = 0;
    while ((typeIndex != index) || (smbiosTable.Hdr->Type != type)) 
    {
        if (smbiosTable.Hdr->Type == SMBIOS_TYPE_END_OF_TABLE) 
        {
            smbiosTable.Raw = 0;
            return smbiosTable;
        }

        if (smbiosTable.Hdr->Type == type) 
        {
            typeIndex++;
        }

        smbiosTable.Raw = (UINT8*)(smbiosTable.Raw + TableLenght(smbiosTable));
    }

    return smbiosTable;
}

UINTN SpaceLength(const char* text, UINTN maxLength) 
{
    if (!text)
        return 0;

    UINTN lenght = 0;

    if (maxLength > 0) 
    {
        for (lenght = 0; lenght < maxLength; lenght++) 
        {
            if (text[lenght] == 0) 
            {
                break;
            }
        }

        if (lenght == 0)
            return 0;

        const char* ba = &text[lenght - 1];

        while ((lenght != 0) && ((*ba == ' ') || (*ba == 0))) 
        {
            ba--; 
            lenght--;
        }
    } 
    else 
    {
        const char* ba = text;
        while (*ba)
        {
            ba++; 
            lenght++;
        }
    }

    return lenght;
}

void EditString(SMBIOS_STRUCTURE_POINTER table, SMBIOS_STRING* field, const char* buffer) 
{
    if (!table.Raw || !buffer || !field)
        return;

    if (*field == 0)
    {
        Print(L"[WARN] String index is 0 (no existing string to overwrite in-place)\r\n");
        return;
    }

    UINT8 index = 1;
    char *astr = (char *)(table.Raw + table.Hdr->Length);
    while (index != *field) 
    {
        if (*astr)
        {
            index++;
        }

        while (*astr != 0)
            astr++;
        astr++;

        if (*astr == 0)
        {
            Print(L"[FAIL] String index %d not found in table\r\n", *field);
            return;
        }
    }

    UINTN astrLength = SpaceLength(astr, 0);
    UINTN bstrLength = SpaceLength(buffer, 256);

    if (astrLength == 0)
    {
        Print(L"[WARN] Existing string length is 0\r\n");
        return;
    }

    if (bstrLength <= astrLength)
    {
        CopyMem(astr, (void *)buffer, bstrLength);
        for (UINTN i = bstrLength; i < astrLength; i++)
        {
            astr[i] = ' ';
        }
        astr[astrLength] = '\0';
    }
    else
    {
        CopyMem(astr, (void *)buffer, astrLength);
        astr[astrLength] = '\0';
        Print(L"[WARN] Replacement string truncated to %d chars (in-place limit)\r\n", astrLength);
    }
}