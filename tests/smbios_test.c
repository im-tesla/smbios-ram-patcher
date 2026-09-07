// Host-side unit tests for src/smbios.c.
//
// The table-editing code is the part of this project that cannot be tried out
// without rebooting, so it is exercised here against a synthetic SMBIOS table
// built in ordinary memory. Compiled natively (no GNU_EFI_USE_MS_ABI); the
// handful of firmware services smbios.c needs are stubbed below.
//
// Run with: make test
#include "general.h"
#include "smbios.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EFI_BOOT_SERVICES    *BS;
EFI_RUNTIME_SERVICES *RT;
EFI_SYSTEM_TABLE     *ST;

static int gVerbose = 0;

VOID LogPrint(LOG_LEVEL level, CONST CHAR16 *fmt, ...) { (void)level; (void)fmt; }
VOID LogRaw(CONST CHAR16 *fmt, ...) { (void)fmt; }

VOID CopyMem(IN VOID *Dest, IN VOID *Src, IN UINTN len) { memmove(Dest, Src, len); }
VOID ZeroMem(IN VOID *Buffer, IN UINTN Size) { memset(Buffer, 0, Size); }
INTN CompareMem(IN CONST VOID *Dest, IN CONST VOID *Src, IN UINTN len) { return memcmp(Dest, Src, len); }

static EFI_STATUS EFIAPI FakeAllocatePages(EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType,
                                    UINTN Pages, EFI_PHYSICAL_ADDRESS *Memory)
{
    (void)Type; (void)MemoryType;
    void *p = aligned_alloc(4096, Pages * 4096);
    if (!p) return EFI_OUT_OF_RESOURCES;
    *Memory = (EFI_PHYSICAL_ADDRESS)(UINTN)p;
    return EFI_SUCCESS;
}

static EFI_BOOT_SERVICES gFakeBs;

// ---------------------------------------------------------------------------
// Synthetic table builder
// ---------------------------------------------------------------------------

static UINT8 *gBuf;
static UINT32 gLen;

static void PutStruct(UINT8 type, UINT8 len, UINT16 handle, const char **strings)
{
    UINT8 *p = gBuf + gLen;
    memset(p, 0, len);
    p[0] = type;
    p[1] = len;
    p[2] = handle & 0xFF;
    p[3] = handle >> 8;
    gLen += len;

    int any = 0;
    for (int i = 0; strings && strings[i]; i++) {
        size_t n = strlen(strings[i]);
        memcpy(gBuf + gLen, strings[i], n + 1);
        gLen += n + 1;
        any = 1;
    }
    if (any) gBuf[gLen++] = 0;
    else { gBuf[gLen++] = 0; gBuf[gLen++] = 0; }
}

// Walks the table and returns 0 if it is structurally sound and its measured
// size matches ctx->TableLength.
static int CheckIntegrity(SMBIOS_CONTEXT *ctx, const char *what)
{
    UINT16 count = 0;
    UINT32 measured = SmbiosGetTotalTableSize(ctx->TableAddress, ctx->TableLength, &count);
    if (measured != ctx->TableLength) {
        printf("  FAIL [%s]: measured %u != ctx->TableLength %u\n", what, measured, ctx->TableLength);
        return 1;
    }
    return 0;
}

static const char *GetStr(SMBIOS_STRUCTURE_POINTER t, UINT8 idx)
{
    static CHAR8 buf[256];
    if (!SmbiosGetString(t, idx, buf, sizeof(buf))) return NULL;
    return (const char *)buf;
}

static int gFailures;

static void Expect(int cond, const char *msg)
{
    if (!cond) { printf("  FAIL: %s\n", msg); gFailures++; }
    else if (gVerbose) printf("  ok: %s\n", msg);
}

static void ExpectStr(const char *got, const char *want, const char *msg)
{
    if ((got == NULL) != (want == NULL) || (got && strcmp(got, want) != 0)) {
        printf("  FAIL: %s (got \"%s\", want \"%s\")\n", msg, got ? got : "(null)", want ? want : "(null)");
        gFailures++;
    } else if (gVerbose) printf("  ok: %s = \"%s\"\n", msg, got ? got : "(null)");
}

// ---------------------------------------------------------------------------

static SMBIOS_CONTEXT MakeCtx(int withHeadroom)
{
    static UINT8 backing[8192];
    gBuf = backing;
    gLen = 0;
    memset(backing, 0xCC, sizeof(backing));

    // Type 1: Manufacturer=1 Product=2 Version=3 Serial=4
    {
        const char *s[] = { "ACME Corp", "SuperBoard", "1.0", "SYS-OLD-SERIAL", NULL };
        UINT8 *start = gBuf + gLen;
        PutStruct(1, 0x1B, 0x0001, s);
        start[0x04] = 1; start[0x05] = 2; start[0x06] = 3; start[0x07] = 4;
        start[0x19] = 0; start[0x1A] = 0;
        memset(start + 0x08, 0xAB, 16);
    }
    // Type 17 #0: locator=1 bank=2 mfg=3 serial=4 asset=5 part=6
    {
        const char *s[] = { "DIMM_A1", "BANK 0", "Kingston", "11223344", "AssetTag0", "KHX-PART", NULL };
        UINT8 *start = gBuf + gLen;
        PutStruct(17, 0x54, 0x0011, s);
        start[0x0C] = 0x00; start[0x0D] = 0x40;   // 16384 MB
        start[0x10] = 1; start[0x11] = 2; start[0x15] = 0x00; start[0x16] = 0x0B;
        start[0x17] = 3; start[0x18] = 4; start[0x19] = 5; start[0x1A] = 6;
    }
    // Type 17 #1: no serial string at all (index 0), other strings present
    {
        const char *s[] = { "DIMM_B1", "BANK 1", "Corsair", "CMK-PART", NULL };
        UINT8 *start = gBuf + gLen;
        PutStruct(17, 0x54, 0x0012, s);
        start[0x0C] = 0x00; start[0x0D] = 0x40;
        start[0x10] = 1; start[0x11] = 2; start[0x15] = 0x00; start[0x16] = 0x0B;
        start[0x17] = 3; start[0x18] = 0; start[0x19] = 0; start[0x1A] = 4;
    }
    // Type 17 #2: empty slot, no strings at all
    {
        UINT8 *start = gBuf + gLen;
        PutStruct(17, 0x54, 0x0013, NULL);
        start[0x18] = 0;
    }
    PutStruct(127, 4, 0x7F00, NULL);

    SMBIOS_CONTEXT ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.Version = SMBIOS_VERSION_3;
    ctx.TableAddress = gBuf;
    ctx.TableLength = gLen;
    ctx.TableMaxAlloc = withHeadroom ? sizeof(backing) : gLen;
    return ctx;
}

int main(int argc, char **argv)
{
    gVerbose = (argc > 1);
    gFakeBs.AllocatePages = FakeAllocatePages;
    BS = &gFakeBs;

    // -----------------------------------------------------------------
    printf("Test 1: table walking\n");
    {
        SMBIOS_CONTEXT ctx = MakeCtx(0);
        UINT16 count = 0;
        UINT32 sz = SmbiosGetTotalTableSize(ctx.TableAddress, 8192, &count);
        Expect(sz == ctx.TableLength, "measured size matches built size");
        Expect(count == 5, "found 5 structures");
        Expect(FindTableByType(&ctx, 1, 0).Raw != NULL, "found type 1");
        Expect(FindTableByType(&ctx, 17, 0).Raw != NULL, "found type 17 #0");
        Expect(FindTableByType(&ctx, 17, 2).Raw != NULL, "found type 17 #2");
        Expect(FindTableByType(&ctx, 17, 3).Raw == NULL, "no type 17 #3");
        Expect(FindTableByType(&ctx, 4, 0).Raw == NULL, "no type 4");
        // Truncated scan must not report a bogus size.
        Expect(SmbiosGetTotalTableSize(ctx.TableAddress, 20, NULL) == 0, "truncated scan returns 0");
    }

    // -----------------------------------------------------------------
    printf("Test 2: same-length serial replacement (in place)\n");
    {
        SMBIOS_CONTEXT ctx = MakeCtx(0);
        UINT32 before = ctx.TableLength;
        SMBIOS_STRUCTURE_POINTER t = FindTableByType(&ctx, 17, 0);
        Expect(SmbiosSetString(&ctx, t, (SMBIOS_STRING *)(t.Raw + 0x18), "FCE1A2B1"), "set 8-char serial");
        Expect(ctx.TableLength == before, "length unchanged");
        Expect(!ctx.IsDynamicAlloc, "no relocation needed");
        t = FindTableByType(&ctx, 17, 0);
        ExpectStr(GetStr(t, t.Raw[0x18]), "FCE1A2B1", "serial");
        ExpectStr(GetStr(t, t.Raw[0x1A]), "KHX-PART", "part number intact");
        ExpectStr(GetStr(t, t.Raw[0x10]), "DIMM_A1", "locator intact");
        gFailures += CheckIntegrity(&ctx, "test2");
    }

    // -----------------------------------------------------------------
    printf("Test 3: shorter serial (table shrinks)\n");
    {
        SMBIOS_CONTEXT ctx = MakeCtx(0);
        UINT32 before = ctx.TableLength;
        SMBIOS_STRUCTURE_POINTER t = FindTableByType(&ctx, 17, 0);
        Expect(SmbiosSetString(&ctx, t, (SMBIOS_STRING *)(t.Raw + 0x18), "ABC"), "set 3-char serial");
        Expect(ctx.TableLength == before - 5, "length shrank by 5");
        t = FindTableByType(&ctx, 17, 0);
        ExpectStr(GetStr(t, t.Raw[0x18]), "ABC", "serial");
        ExpectStr(GetStr(t, t.Raw[0x19]), "AssetTag0", "asset tag intact");
        ExpectStr(GetStr(t, t.Raw[0x1A]), "KHX-PART", "part number intact");
        SMBIOS_STRUCTURE_POINTER t2 = FindTableByType(&ctx, 17, 1);
        ExpectStr(GetStr(t2, t2.Raw[0x10]), "DIMM_B1", "next structure intact");
        gFailures += CheckIntegrity(&ctx, "test3");
    }

    // -----------------------------------------------------------------
    printf("Test 4: longer serial forces relocation (no headroom)\n");
    {
        SMBIOS_CONTEXT ctx = MakeCtx(0);
        UINT32 before = ctx.TableLength;
        UINT8 *origAddr = ctx.TableAddress;
        SMBIOS_STRUCTURE_POINTER t = FindTableByType(&ctx, 17, 0);
        Expect(SmbiosSetString(&ctx, t, (SMBIOS_STRING *)(t.Raw + 0x18), "AVERYLONGSERIALNUMBER"), "set 21-char serial");
        Expect(ctx.IsDynamicAlloc, "table was relocated");
        Expect(ctx.TableAddress != origAddr, "address changed");
        Expect(ctx.TableLength == before + 13, "length grew by 13");
        t = FindTableByType(&ctx, 17, 0);
        ExpectStr(GetStr(t, t.Raw[0x18]), "AVERYLONGSERIALNUMBER", "serial");
        ExpectStr(GetStr(t, t.Raw[0x1A]), "KHX-PART", "part number intact");
        SMBIOS_STRUCTURE_POINTER t2 = FindTableByType(&ctx, 17, 2);
        Expect(t2.Raw != NULL && t2.Hdr->Length == 0x54, "last memory device intact");
        gFailures += CheckIntegrity(&ctx, "test4");
    }

    // -----------------------------------------------------------------
    printf("Test 5: add a serial to a slot that has none (index 0)\n");
    {
        SMBIOS_CONTEXT ctx = MakeCtx(0);
        SMBIOS_STRUCTURE_POINTER t = FindTableByType(&ctx, 17, 1);
        Expect(t.Raw[0x18] == 0, "starts with no serial string");
        Expect(SmbiosSetString(&ctx, t, (SMBIOS_STRING *)(t.Raw + 0x18), "NEWSER01"), "append serial");
        t = FindTableByType(&ctx, 17, 1);
        Expect(t.Raw[0x18] == 5, "serial became string #5");
        ExpectStr(GetStr(t, t.Raw[0x18]), "NEWSER01", "serial");
        ExpectStr(GetStr(t, t.Raw[0x10]), "DIMM_B1", "locator intact");
        ExpectStr(GetStr(t, t.Raw[0x1A]), "CMK-PART", "part number intact");
        SMBIOS_STRUCTURE_POINTER t0 = FindTableByType(&ctx, 17, 0);
        ExpectStr(GetStr(t0, t0.Raw[0x18]), "11223344", "previous structure intact");
        gFailures += CheckIntegrity(&ctx, "test5");
    }

    // -----------------------------------------------------------------
    printf("Test 6: add a serial to a structure with no strings at all\n");
    {
        SMBIOS_CONTEXT ctx = MakeCtx(0);
        UINT32 before = ctx.TableLength;
        SMBIOS_STRUCTURE_POINTER t = FindTableByType(&ctx, 17, 2);
        Expect(SmbiosSetString(&ctx, t, (SMBIOS_STRING *)(t.Raw + 0x18), "EMPTY001"), "append first string");
        Expect(ctx.TableLength == before + 8, "length grew by exactly 8");
        t = FindTableByType(&ctx, 17, 2);
        Expect(t.Raw[0x18] == 1, "serial became string #1");
        ExpectStr(GetStr(t, t.Raw[0x18]), "EMPTY001", "serial");
        SMBIOS_STRUCTURE_POINTER term = FindTableByType(&ctx, 127, 0);
        Expect(term.Raw != NULL, "terminator still reachable");
        gFailures += CheckIntegrity(&ctx, "test6");
    }

    // -----------------------------------------------------------------
    printf("Test 7: remove a serial, indices after it shift down\n");
    {
        SMBIOS_CONTEXT ctx = MakeCtx(0);
        UINT32 before = ctx.TableLength;
        SMBIOS_STRUCTURE_POINTER t = FindTableByType(&ctx, 17, 0);
        Expect(SmbiosSetString(&ctx, t, (SMBIOS_STRING *)(t.Raw + 0x18), NULL), "remove serial");
        Expect(ctx.TableLength == before - 9, "length shrank by strlen+1");
        t = FindTableByType(&ctx, 17, 0);
        Expect(t.Raw[0x18] == 0, "serial index cleared");
        Expect(t.Raw[0x19] == 4, "asset tag index shifted 5 -> 4");
        Expect(t.Raw[0x1A] == 5, "part number index shifted 6 -> 5");
        Expect(t.Raw[0x10] == 1, "locator index unchanged");
        ExpectStr(GetStr(t, t.Raw[0x19]), "AssetTag0", "asset tag still resolves");
        ExpectStr(GetStr(t, t.Raw[0x1A]), "KHX-PART", "part number still resolves");
        gFailures += CheckIntegrity(&ctx, "test7");
    }

    // -----------------------------------------------------------------
    printf("Test 8: remove the only string of a structure\n");
    {
        SMBIOS_CONTEXT ctx = MakeCtx(0);
        SMBIOS_STRUCTURE_POINTER t = FindTableByType(&ctx, 17, 2);
        SmbiosSetString(&ctx, t, (SMBIOS_STRING *)(t.Raw + 0x18), "ONLYONE");
        UINT32 mid = ctx.TableLength;
        t = FindTableByType(&ctx, 17, 2);
        Expect(SmbiosSetString(&ctx, t, (SMBIOS_STRING *)(t.Raw + 0x18), NULL), "remove it again");
        Expect(ctx.TableLength == mid - 7, "length back down by 7");
        t = FindTableByType(&ctx, 17, 2);
        Expect(t.Raw[0x18] == 0, "serial index cleared");
        Expect(t.Raw[t.Hdr->Length] == 0 && t.Raw[t.Hdr->Length + 1] == 0, "double NUL restored");
        gFailures += CheckIntegrity(&ctx, "test8");
    }

    // -----------------------------------------------------------------
    printf("Test 9: type 1 serial + UUID, mirroring the real patch order\n");
    {
        SMBIOS_CONTEXT ctx = MakeCtx(0);
        SMBIOS_STRUCTURE_POINTER t = FindTableByType(&ctx, 1, 0);
        UINT8 uuid[16];
        for (int i = 0; i < 16; i++) uuid[i] = (UINT8)(0x10 + i);
        memcpy(t.Raw + 0x08, uuid, 16);
        Expect(SmbiosSetString(&ctx, t, (SMBIOS_STRING *)(t.Raw + 0x07), NULL), "remove system serial");
        t = FindTableByType(&ctx, 1, 0);
        Expect(memcmp(t.Raw + 0x08, uuid, 16) == 0, "UUID survived the string edit");
        Expect(t.Raw[0x07] == 0, "serial index cleared");
        ExpectStr(GetStr(t, t.Raw[0x04]), "ACME Corp", "manufacturer intact");
        ExpectStr(GetStr(t, t.Raw[0x06]), "1.0", "version intact");
        gFailures += CheckIntegrity(&ctx, "test9");
    }

    // -----------------------------------------------------------------
    printf("Test 10: repeated edits across every memory slot\n");
    {
        SMBIOS_CONTEXT ctx = MakeCtx(0);
        for (int round = 0; round < 3; round++) {
            const char *serials[] = { "S1", "MEDIUMSERIAL", "X" };
            for (int i = 0; i < 3; i++) {
                SMBIOS_STRUCTURE_POINTER t = FindTableByType(&ctx, 17, i);
                Expect(t.Raw != NULL, "slot present");
                Expect(SmbiosSetString(&ctx, t, (SMBIOS_STRING *)(t.Raw + 0x18), serials[round]), "set serial");
                if (CheckIntegrity(&ctx, "test10")) { gFailures++; goto done10; }
            }
            for (int i = 0; i < 3; i++) {
                SMBIOS_STRUCTURE_POINTER t = FindTableByType(&ctx, 17, i);
                ExpectStr(GetStr(t, t.Raw[0x18]), serials[round], "serial round-trip");
            }
        }
    done10:;
        SMBIOS_STRUCTURE_POINTER t1 = FindTableByType(&ctx, 1, 0);
        ExpectStr(GetStr(t1, t1.Raw[0x05]), "SuperBoard", "type 1 untouched");
    }

    printf("\n%s (%d failure%s)\n", gFailures ? "FAILED" : "ALL TESTS PASSED",
           gFailures, gFailures == 1 ? "" : "s");
    return gFailures ? 1 : 0;
}
