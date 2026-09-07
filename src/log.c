#include "general.h"
#include "log.h"
#include "editme.h"

#include <stdarg.h>

#define LOG_BUFFER_CHARS 1024

static SIMPLE_TEXT_OUTPUT_INTERFACE *gConOut = NULL;
static SIMPLE_INPUT_INTERFACE       *gConIn  = NULL;
static LOG_LEVEL                     gMinLevel = LOG_LEVEL_DEBUG;
static BOOLEAN                       gPaging = FALSE;
static UINTN                         gScreenRows = 25;
static UINTN                         gLinesSincePause = 0;
static UINTN                         gCurrentAttr = EFI_LIGHTGRAY;

// ---------------------------------------------------------------------------
// Low level console output
// ---------------------------------------------------------------------------

static VOID LogSetAttr(UINTN attr)
{
    if (gConOut == NULL || attr == gCurrentAttr)
        return;

    gConOut->SetAttribute(gConOut, EFI_TEXT_ATTR(attr, EFI_BACKGROUND_BLACK));
    gCurrentAttr = attr;
}

static VOID LogOutputRun(CONST CHAR16 *start, UINTN count)
{
    CHAR16 chunk[128];

    while (count > 0)
    {
        UINTN n = (count < (sizeof(chunk) / sizeof(CHAR16)) - 1)
                      ? count
                      : (sizeof(chunk) / sizeof(CHAR16)) - 1;

        for (UINTN i = 0; i < n; i++)
            chunk[i] = start[i];
        chunk[n] = L'\0';

        gConOut->OutputString(gConOut, chunk);

        start += n;
        count -= n;
    }
}

static VOID LogDrainKeys(VOID)
{
    EFI_INPUT_KEY key;

    if (gConIn == NULL)
        return;

    while (gConIn->ReadKeyStroke(gConIn, &key) == EFI_SUCCESS)
        ;
}

// Waits for a key. Returns TRUE if a key was pressed, FALSE on timeout.
// `seconds` of 0 waits indefinitely.
static BOOLEAN LogWaitKey(UINTN seconds)
{
    EFI_INPUT_KEY key;
    EFI_EVENT     events[2];
    UINTN         eventCount = 1;
    UINTN         index = 0;
    EFI_EVENT     timer = NULL;
    EFI_STATUS    status;

    if (gConIn == NULL || gBS == NULL)
        return FALSE;

    LogDrainKeys();
    events[0] = gConIn->WaitForKey;

    if (seconds > 0)
    {
        status = gBS->CreateEvent(EVT_TIMER, TPL_CALLBACK, NULL, NULL, &timer);
        if (!EFI_ERROR(status))
        {
            // Relative timer, 100ns units.
            gBS->SetTimer(timer, TimerRelative, (UINT64)seconds * 10000000ULL);
            events[1] = timer;
            eventCount = 2;
        }
    }

    status = gBS->WaitForEvent(eventCount, events, &index);

    if (timer != NULL)
    {
        gBS->SetTimer(timer, TimerCancel, 0);
        gBS->CloseEvent(timer);
    }

    if (EFI_ERROR(status))
        return FALSE;

    if (index == 0)
    {
        gConIn->ReadKeyStroke(gConIn, &key);
        return TRUE;
    }

    return FALSE;
}

// Never blocks forever: a machine with no usable keyboard must still finish
// booting.
#define LOG_PAGE_PAUSE_TIMEOUT 60

static VOID LogPagePause(VOID)
{
    UINTN savedAttr = gCurrentAttr;

    LogSetAttr(EFI_BLACK | EFI_BRIGHT);
    gConOut->OutputString(gConOut, L"-- More -- (press any key)");

    LogWaitKey(LOG_PAGE_PAUSE_TIMEOUT);

    // Wipe the prompt so it does not pollute the transcript.
    gConOut->OutputString(gConOut, L"\r                          \r");
    LogSetAttr(savedAttr);

    gLinesSincePause = 0;
}

// Writes `s` to the console, expanding "\n" to "\r\n" and applying paging.
static VOID LogEmit(CONST CHAR16 *s)
{
    if (gConOut == NULL || s == NULL)
        return;

    while (*s != L'\0')
    {
        CONST CHAR16 *runStart = s;

        while (*s != L'\0' && *s != L'\n')
            s++;

        if (s != runStart)
            LogOutputRun(runStart, (UINTN)(s - runStart));

        if (*s == L'\n')
        {
            gConOut->OutputString(gConOut, L"\r\n");
            s++;
            gLinesSincePause++;

            if (gPaging && gScreenRows > 2 && gLinesSincePause >= gScreenRows - 2)
                LogPagePause();
        }
    }
}

// ---------------------------------------------------------------------------
// Formatter
// ---------------------------------------------------------------------------

typedef struct {
    CHAR16 *Buffer;
    UINTN   Capacity;   // in CHAR16, including room for the terminator
    UINTN   Length;
} LOG_SINK;

static VOID SinkChar(LOG_SINK *sink, CHAR16 c)
{
    if (sink->Length + 1 < sink->Capacity)
        sink->Buffer[sink->Length++] = c;
}

static VOID SinkWide(LOG_SINK *sink, CONST CHAR16 *s)
{
    if (s == NULL)
        s = L"(null)";

    while (*s != L'\0')
        SinkChar(sink, *s++);
}

static VOID SinkAscii(LOG_SINK *sink, CONST CHAR8 *s)
{
    if (s == NULL)
        s = (CONST CHAR8*)"(null)";

    while (*s != '\0')
        SinkChar(sink, (CHAR16)(UINT8)*s++);
}

static VOID SinkPadded(LOG_SINK *sink, CONST CHAR16 *text, UINTN len,
                       UINTN width, BOOLEAN zeroPad, BOOLEAN negative)
{
    UINTN total = len + (negative ? 1 : 0);

    if (negative && zeroPad)
        SinkChar(sink, L'-');

    while (total < width)
    {
        SinkChar(sink, zeroPad ? L'0' : L' ');
        total++;
    }

    if (negative && !zeroPad)
        SinkChar(sink, L'-');

    for (UINTN i = 0; i < len; i++)
        SinkChar(sink, text[i]);
}

static UINTN UnsignedToDigits(UINT64 value, UINTN base, BOOLEAN upper,
                              CHAR16 *out, UINTN outLen)
{
    CONST CHAR16 *digits = upper ? L"0123456789ABCDEF" : L"0123456789abcdef";
    CHAR16 tmp[24];
    UINTN  n = 0;

    if (value == 0)
        tmp[n++] = L'0';

    while (value != 0 && n < sizeof(tmp) / sizeof(CHAR16))
    {
        tmp[n++] = digits[value % base];
        value /= base;
    }

    if (n > outLen)
        n = outLen;

    for (UINTN i = 0; i < n; i++)
        out[i] = tmp[n - 1 - i];

    return n;
}

static VOID SinkNumber(LOG_SINK *sink, UINT64 value, UINTN base, BOOLEAN upper,
                       UINTN width, BOOLEAN zeroPad, BOOLEAN negative)
{
    CHAR16 digits[24];
    UINTN  len = UnsignedToDigits(value, base, upper, digits,
                                  sizeof(digits) / sizeof(CHAR16));

    SinkPadded(sink, digits, len, width, zeroPad, negative);
}

static VOID SinkStatus(LOG_SINK *sink, EFI_STATUS status)
{
    CONST CHAR16 *text = NULL;

    switch (status)
    {
        case EFI_SUCCESS:            text = L"Success";            break;
        case EFI_LOAD_ERROR:         text = L"Load Error";         break;
        case EFI_INVALID_PARAMETER:  text = L"Invalid Parameter";  break;
        case EFI_UNSUPPORTED:        text = L"Unsupported";        break;
        case EFI_BAD_BUFFER_SIZE:    text = L"Bad Buffer Size";    break;
        case EFI_BUFFER_TOO_SMALL:   text = L"Buffer Too Small";   break;
        case EFI_NOT_READY:          text = L"Not Ready";          break;
        case EFI_DEVICE_ERROR:       text = L"Device Error";       break;
        case EFI_WRITE_PROTECTED:    text = L"Write Protected";    break;
        case EFI_OUT_OF_RESOURCES:   text = L"Out Of Resources";   break;
        case EFI_NOT_FOUND:          text = L"Not Found";          break;
        case EFI_ACCESS_DENIED:      text = L"Access Denied";      break;
        case EFI_TIMEOUT:            text = L"Timeout";            break;
        case EFI_ABORTED:            text = L"Aborted";            break;
        case EFI_SECURITY_VIOLATION: text = L"Security Violation"; break;
        default:                                                   break;
    }

    if (text != NULL)
    {
        SinkWide(sink, text);
    }
    else
    {
        SinkWide(sink, L"Status 0x");
        SinkNumber(sink, (UINT64)status, 16, FALSE, 16, TRUE, FALSE);
    }
}

static VOID SinkGuid(LOG_SINK *sink, CONST EFI_GUID *guid)
{
    if (guid == NULL)
    {
        SinkWide(sink, L"(null)");
        return;
    }

    SinkNumber(sink, guid->Data1, 16, FALSE, 8, TRUE, FALSE);
    SinkChar(sink, L'-');
    SinkNumber(sink, guid->Data2, 16, FALSE, 4, TRUE, FALSE);
    SinkChar(sink, L'-');
    SinkNumber(sink, guid->Data3, 16, FALSE, 4, TRUE, FALSE);
    SinkChar(sink, L'-');
    SinkNumber(sink, guid->Data4[0], 16, FALSE, 2, TRUE, FALSE);
    SinkNumber(sink, guid->Data4[1], 16, FALSE, 2, TRUE, FALSE);
    SinkChar(sink, L'-');
    for (UINTN i = 2; i < 8; i++)
        SinkNumber(sink, guid->Data4[i], 16, FALSE, 2, TRUE, FALSE);
}

static VOID LogFormat(CHAR16 *out, UINTN outChars, CONST CHAR16 *fmt, va_list args)
{
    LOG_SINK sink;

    sink.Buffer = out;
    sink.Capacity = outChars;
    sink.Length = 0;

    if (outChars == 0)
        return;

    while (*fmt != L'\0')
    {
        if (*fmt != L'%')
        {
            SinkChar(&sink, *fmt++);
            continue;
        }

        fmt++;

        BOOLEAN zeroPad = FALSE;
        UINTN   width = 0;
        BOOLEAN isLong = FALSE;

        if (*fmt == L'0')
        {
            zeroPad = TRUE;
            fmt++;
        }

        while (*fmt >= L'0' && *fmt <= L'9')
        {
            width = width * 10 + (UINTN)(*fmt - L'0');
            fmt++;
        }

        while (*fmt == L'l' || *fmt == L'L')
        {
            isLong = TRUE;
            fmt++;
        }

        switch (*fmt)
        {
            case L'\0':
                return;

            case L'%':
                SinkChar(&sink, L'%');
                break;

            case L'c':
                SinkChar(&sink, (CHAR16)va_arg(args, UINTN));
                break;

            case L's':
                SinkWide(&sink, va_arg(args, CHAR16*));
                break;

            case L'a':
                SinkAscii(&sink, va_arg(args, CHAR8*));
                break;

            case L'd':
            case L'i':
            {
                INT64 v = isLong ? va_arg(args, INT64) : (INT64)va_arg(args, INT32);
                BOOLEAN neg = (v < 0);
                UINT64 mag = neg ? (UINT64)(-v) : (UINT64)v;
                SinkNumber(&sink, mag, 10, FALSE, width, zeroPad, neg);
                break;
            }

            case L'u':
            {
                UINT64 v = isLong ? va_arg(args, UINT64) : (UINT64)va_arg(args, UINT32);
                SinkNumber(&sink, v, 10, FALSE, width, zeroPad, FALSE);
                break;
            }

            case L'x':
            case L'X':
            {
                UINT64 v = isLong ? va_arg(args, UINT64) : (UINT64)va_arg(args, UINT32);
                SinkNumber(&sink, v, 16, (*fmt == L'X'), width, zeroPad, FALSE);
                break;
            }

            case L'p':
                SinkWide(&sink, L"0x");
                SinkNumber(&sink, (UINT64)(UINTN)va_arg(args, VOID*), 16, FALSE,
                           16, TRUE, FALSE);
                break;

            case L'r':
                SinkStatus(&sink, (EFI_STATUS)va_arg(args, UINTN));
                break;

            case L'g':
                SinkGuid(&sink, va_arg(args, EFI_GUID*));
                break;

            default:
                SinkChar(&sink, L'%');
                SinkChar(&sink, *fmt);
                break;
        }

        fmt++;
    }

    sink.Buffer[sink.Length] = L'\0';
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

VOID LogInit(EFI_SYSTEM_TABLE *SystemTable)
{
    gConOut = NULL;
    gConIn = NULL;
    gScreenRows = 25;
    gLinesSincePause = 0;
    gCurrentAttr = (UINTN)-1;

    if (SystemTable == NULL)
        return;

    gConOut = SystemTable->ConOut;
    gConIn = SystemTable->ConIn;

    if (gConOut != NULL)
    {
        UINTN cols = 0;
        UINTN rows = 0;

        if (gConOut->Mode != NULL &&
            gConOut->QueryMode(gConOut, gConOut->Mode->Mode, &cols, &rows) == EFI_SUCCESS &&
            rows > 4)
        {
            gScreenRows = rows;
        }

        LogSetAttr(EFI_LIGHTGRAY);
    }

    LogSetLevel((LOG_LEVEL)LOG_MIN_LEVEL);
    LogSetPaging(LOG_PAGING ? TRUE : FALSE);
}

VOID LogSetLevel(LOG_LEVEL level)
{
    gMinLevel = level;
}

VOID LogSetPaging(BOOLEAN enabled)
{
    gPaging = enabled;
    gLinesSincePause = 0;
}

VOID LogPrint(LOG_LEVEL level, CONST CHAR16 *fmt, ...)
{
    CHAR16  buffer[LOG_BUFFER_CHARS];
    va_list args;

    if (gConOut == NULL || level < gMinLevel)
        return;

    CONST CHAR16 *tag = L"[INFO   ] ";
    UINTN attr = EFI_LIGHTGRAY;

    switch (level)
    {
        case LOG_LEVEL_DEBUG:   tag = L"[DEBUG  ] "; attr = EFI_DARKGRAY;   break;
        case LOG_LEVEL_INFO:    tag = L"[INFO   ] "; attr = EFI_LIGHTGRAY;  break;
        case LOG_LEVEL_SUCCESS: tag = L"[SUCCESS] "; attr = EFI_LIGHTGREEN; break;
        case LOG_LEVEL_WARN:    tag = L"[WARN   ] "; attr = EFI_YELLOW;     break;
        case LOG_LEVEL_ERROR:   tag = L"[ERROR  ] "; attr = EFI_LIGHTRED;   break;
        default:                                                            break;
    }

    va_start(args, fmt);
    LogFormat(buffer, LOG_BUFFER_CHARS, fmt, args);
    va_end(args);

    LogSetAttr(attr);
    LogEmit(tag);
    LogEmit(buffer);
    LogSetAttr(EFI_LIGHTGRAY);
}

VOID LogRaw(CONST CHAR16 *fmt, ...)
{
    CHAR16  buffer[LOG_BUFFER_CHARS];
    va_list args;

    if (gConOut == NULL)
        return;

    va_start(args, fmt);
    LogFormat(buffer, LOG_BUFFER_CHARS, fmt, args);
    va_end(args);

    LogEmit(buffer);
}

VOID LogWaitKeyOrTimeout(UINTN seconds)
{
    if (gConOut == NULL)
        return;

    LogSetAttr(EFI_WHITE);

    if (seconds > 0)
        LogRaw(L"Press any key to continue (auto-continue in %u seconds)...\n", (UINT32)seconds);
    else
        LogRaw(L"Press any key to continue...\n");

    LogWaitKey(seconds);
    LogSetAttr(EFI_LIGHTGRAY);
}

VOID LogClose(VOID)
{
    if (gConOut != NULL)
        LogSetAttr(EFI_LIGHTGRAY);
}
