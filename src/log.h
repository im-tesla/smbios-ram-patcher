#ifndef LOG_H
#define LOG_H

#include "general.h"

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_SUCCESS,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_NONE
} LOG_LEVEL;

// Console logger. All output goes to the active text console via
// SystemTable->ConOut, colour coded per level.
//
// The formatter is self contained (see LogFormat in log.c) so the exact set of
// supported conversions is known and does not depend on the gnu-efi Print
// implementation:
//
//   %%            literal percent
//   %c            CHAR16
//   %s            CHAR16*  (wide string)
//   %a            CHAR8*   (ascii string)
//   %d %i         signed decimal   (%ld for 64-bit)
//   %u            unsigned decimal (%lu for 64-bit)
//   %x %X         hexadecimal      (%lx for 64-bit)
//   %p            pointer
//   %r            EFI_STATUS
//   %g            EFI_GUID*
//
// Field width and zero padding work as expected: %02x, %08x, %5u, ...
// A "\n" in the format string is emitted as "\r\n"; no need to write "\r\n".

VOID LogInit(EFI_SYSTEM_TABLE *SystemTable);
VOID LogSetLevel(LOG_LEVEL level);
VOID LogSetPaging(BOOLEAN enabled);
VOID LogPrint(LOG_LEVEL level, CONST CHAR16 *fmt, ...);

// Prints without a level tag, using the current colour.
VOID LogRaw(CONST CHAR16 *fmt, ...);

// Resets colours; call before returning from efi_main.
VOID LogClose(VOID);

// Waits for a keypress, or up to `seconds` (0 = wait forever).
VOID LogWaitKeyOrTimeout(UINTN seconds);

#define LOG_DEBUG(...)   LogPrint(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)    LogPrint(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_SUCCESS(...) LogPrint(LOG_LEVEL_SUCCESS, __VA_ARGS__)
#define LOG_WARN(...)    LogPrint(LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_ERROR(...)   LogPrint(LOG_LEVEL_ERROR, __VA_ARGS__)

#endif
