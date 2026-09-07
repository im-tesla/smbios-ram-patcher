#include "general.h"
#include "utils.h"

static UINT32 gRandomSeed = 0;

static UINT32 NextRandom(VOID)
{
    if (gRandomSeed == 0)
    {
        EFI_TIME time;
        EFI_TIME_CAPABILITIES cap;
        if (RT && RT->GetTime && RT->GetTime(&time, &cap) == EFI_SUCCESS)
        {
            gRandomSeed = (UINT32)(time.Year ^ (time.Month << 16) ^ (time.Day << 24) ^
                                   (time.Hour << 12) ^ (time.Minute << 6) ^
                                   time.Second ^ time.Nanosecond);
        }
        else
        {
            gRandomSeed = 0xA5B3C1D7;
        }

        if (gRandomSeed == 0)
            gRandomSeed = 0x12345678;
    }

    // 32-bit Xorshift PRNG
    gRandomSeed ^= gRandomSeed << 13;
    gRandomSeed ^= gRandomSeed >> 17;
    gRandomSeed ^= gRandomSeed << 5;

    return gRandomSeed;
}

int RandomNumber(int l, int h)
{
    if (l >= h)
        return l;

    UINT32 range = (UINT32)(h - l + 1);
    UINT32 randVal = NextRandom();

    return (int)(l + (randVal % range));
}

void RandomText(char* s, const int len)
{
    if (!s || len <= 0)
        return;

    static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const UINT32 charsetSize = (UINT32)(sizeof(charset) - 1);

    for (int i = 0; i < len; i++)
    {
        UINT32 r = NextRandom() % charsetSize;
        s[i] = charset[r];
    }

    s[len] = '\0';
}