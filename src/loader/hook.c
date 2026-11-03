/* Code patches in Game.exe. Every patch checks the original bytes first, so a different exe is never changed. */
#include "onc.h"
#include <string.h>

static int unprotect_write(void *p, const void *src, size_t n) {
    DWORD old;
    if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(p, src, n);
    VirtualProtect(p, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, n);
    return 1;
}

int onc_hook_jmp(const char *name, uintptr_t addr, void *repl, const uint8_t *expect, size_t n) {
    if (n < 5 || memcmp((void *)addr, expect, n) != 0) {
        onc_log("HOOK %s %08X refused: original bytes differ", name, (unsigned)addr);
        return 0;
    }
    uint8_t j[5] = {0xE9};
    int32_t rel = (int32_t)((uintptr_t)repl - (addr + 5));
    memcpy(j + 1, &rel, 4);
    return unprotect_write((void *)addr, j, 5);
}

void *onc_hook_detour(const char *name, uintptr_t addr, void *repl, const uint8_t *expect, size_t n) {
    if (n < 5 || memcmp((void *)addr, expect, n) != 0) {
        onc_log("HOOK %s %08X refused: original bytes differ", name, (unsigned)addr);
        return NULL;
    }
    uint8_t *t = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return NULL;
    memcpy(t, (void *)addr, n); /* the copied instructions must not be relative (checked by the caller) */
    int32_t rel = (int32_t)((addr + n) - ((uintptr_t)t + n + 5));
    t[n] = 0xE9;
    memcpy(t + n + 1, &rel, 4);
    FlushInstructionCache(GetCurrentProcess(), t, n + 5);
    if (!onc_hook_jmp(name, addr, repl, expect, n)) { VirtualFree(t, 0, MEM_RELEASE); return NULL; }
    onc_log("hooked %s (%08X)", name, (unsigned)addr);
    return t;
}

int onc_hook_call(const char *name, uintptr_t call_addr, void *repl, uintptr_t expect_target) {
    uint8_t *p = (uint8_t *)call_addr;
    if (p[0] != 0xE8 || call_addr + 5 + *(int32_t *)(p + 1) != expect_target) {
        onc_log("HOOK %s %08X refused: not the expected call", name, (unsigned)call_addr);
        return 0;
    }
    int32_t rel = (int32_t)((uintptr_t)repl - (call_addr + 5));
    if (!unprotect_write(p + 1, &rel, 4)) return 0;
    onc_log("hooked %s (call at %08X)", name, (unsigned)call_addr);
    return 1;
}
