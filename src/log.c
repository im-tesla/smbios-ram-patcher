#include <stdarg.h>
#include "general.h"
#include "log.h"

static LOG_LEVEL gMinLogLevel = LOG_LEVEL_INFO;

VOID LogInit(VOID)
{
    if (ST && ST->ConOut)
    {
        ST->ConOut->SetAttribute(ST->ConOut, EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK));
    }
}

VOID LogSetLevel(LOG_LEVEL level)
{
    gMinLogLevel = level;
}

VOID LogPrint(LOG_LEVEL level, CONST CHAR16 *fmt, ...)
{
    if (level < gMinLogLevel)
        return;

    va_list args;
    va_start(args, fmt);

    INT32 prevAttr = EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK);
    if (ST && ST->ConOut && ST->ConOut->Mode)
    {
        prevAttr = ST->ConOut->Mode->Attribute;
        switch (level)
        {
            case LOG_LEVEL_DEBUG:
                ST->ConOut->SetAttribute(ST->ConOut, EFI_TEXT_ATTR(EFI_DARKGRAY, EFI_BLACK));
                Print(L"[DEBUG] ");
                break;
            case LOG_LEVEL_INFO:
                ST->ConOut->SetAttribute(ST->ConOut, EFI_TEXT_ATTR(EFI_CYAN, EFI_BLACK));
                Print(L"[INFO]  ");
                break;
            case LOG_LEVEL_SUCCESS:
                ST->ConOut->SetAttribute(ST->ConOut, EFI_TEXT_ATTR(EFI_LIGHTGREEN, EFI_BLACK));
                Print(L"[OK]    ");
                break;
            case LOG_LEVEL_WARN:
                ST->ConOut->SetAttribute(ST->ConOut, EFI_TEXT_ATTR(EFI_YELLOW, EFI_BLACK));
                Print(L"[WARN]  ");
                break;
            case LOG_LEVEL_ERROR:
                ST->ConOut->SetAttribute(ST->ConOut, EFI_TEXT_ATTR(EFI_LIGHTRED, EFI_BLACK));
                Print(L"[ERROR] ");
                break;
        }
        ST->ConOut->SetAttribute(ST->ConOut, EFI_TEXT_ATTR(EFI_WHITE, EFI_BLACK));
    }

    VPrint(fmt, args);
    va_end(args);

    if (ST && ST->ConOut)
    {
        ST->ConOut->SetAttribute(ST->ConOut, prevAttr);
    }
}

VOID LogWaitKeyOrTimeout(UINTN seconds)
{
    if (seconds == 0 || !ST || !ST->ConIn || !BS)
        return;

    Print(L"\r\nWaiting %u seconds (press any key to continue)...\r\n", (UINT32)seconds);

    EFI_INPUT_KEY key;
    // Clear any pending keystroke
    while (ST->ConIn->ReadKeyStroke(ST->ConIn, &key) == EFI_SUCCESS)
        ;

    for (UINTN s = 0; s < seconds; s++)
    {
        for (UINTN i = 0; i < 10; i++)
        {
            if (ST->ConIn->ReadKeyStroke(ST->ConIn, &key) == EFI_SUCCESS)
            {
                return;
            }
            BS->Stall(100000); // 100ms
        }
    }
}
