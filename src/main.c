#include "general.h"
#include "log.h"
#include "finder.h"
#include "patch.h"
#include "smbios.h"
#include "editme.h"

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);
    LogInit(SystemTable);

    LogRaw(L"\n");
    LOG_INFO(L"SMBIOS patcher - System UUID and memory serial numbers\n");

    if (SystemTable != NULL)
    {
        if (SystemTable->FirmwareVendor != NULL)
            LOG_DEBUG(L"Firmware vendor:   %s\n", SystemTable->FirmwareVendor);

        LOG_DEBUG(L"Firmware revision: 0x%08x\n", SystemTable->FirmwareRevision);
        LOG_DEBUG(L"UEFI revision:     %u.%02u\n",
                  (UINT32)(SystemTable->Hdr.Revision >> 16),
                  (UINT32)(SystemTable->Hdr.Revision & 0xFFFF));
    }

    SMBIOS_CONTEXT_LIST smbiosList;
    ZeroMem(&smbiosList, sizeof(smbiosList));

    LOG_INFO(L"Searching for SMBIOS tables...\n");

    if (!FindAllSmbios(&smbiosList) || smbiosList.Count == 0)
    {
        LOG_ERROR(L"No SMBIOS table entry point could be located; nothing was changed\n");
        LogWaitKeyOrTimeout(LOG_PAUSE_SECONDS);
        LogClose();
        return EFI_NOT_FOUND;
    }

    LOG_INFO(L"Found %u SMBIOS table instance(s)\n", (UINT32)smbiosList.Count);

    PATCH_STATS stats;
    ZeroMem(&stats, sizeof(stats));

    BOOLEAN patched = PatchAll(&smbiosList, &stats);

    LogRaw(L"\n");
    LOG_INFO(L"Summary: %u system structure(s), %u memory slot(s) patched, "
             L"%u slot(s) skipped, %u failure(s)\n",
             (UINT32)stats.Type1Patched, (UINT32)stats.Type17Patched,
             (UINT32)stats.Type17Skipped, (UINT32)stats.Failures);

    if (patched && stats.Failures == 0)
        LOG_SUCCESS(L"SMBIOS tables patched successfully\n");
    else if (patched)
        LOG_WARN(L"SMBIOS tables patched, but some edits failed\n");
    else
        LOG_ERROR(L"Nothing was patched\n");

    LogWaitKeyOrTimeout(LOG_PAUSE_SECONDS);
    LogClose();

    return patched ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
