#include "general.h"
#include "log.h"
#include "finder.h"
#include "patch.h"
#include "smbios.h"

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) 
{
    InitializeLib(ImageHandle, SystemTable);
    LogInit();

    LOG_INFO(L"=========================================\r\n");
    LOG_INFO(L"      SMBIOS Table Patcher (HWID)        \r\n");
    LOG_INFO(L"=========================================\r\n");

    SMBIOS_CONTEXT ctx;
    ZeroMem(&ctx, sizeof(ctx));

    LOG_INFO(L"Searching for SMBIOS table entry point...\r\n");
    if (!FindSmbios(&ctx)) 
    {
        LOG_ERROR(L"Failed to locate SMBIOS table entry point\r\n");
        LogWaitKeyOrTimeout(GetLogPauseSeconds());
        return EFI_NOT_FOUND;
    }

    LOG_INFO(L"Beginning SMBIOS table patching...\r\n");
    BOOLEAN patchResult = PatchAll(&ctx);

    if (patchResult)
    {
        LOG_SUCCESS(L"SMBIOS tables patched successfully!\r\n");
    }
    else
    {
        LOG_WARN(L"SMBIOS patching finished with warnings or no matching slots\r\n");
    }

    LogWaitKeyOrTimeout(GetLogPauseSeconds());

    return patchResult ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}