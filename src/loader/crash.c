/* Logs access violations (address, registers, stack) to nox-modernized.log, so a crash can be traced. */
#include "onc.h"
#include <string.h>

static LONG g_count;

static void mod_of(uintptr_t a, char *out, int n) {
    HMODULE m = NULL;
    out[0] = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) && m) {
        char p[MAX_PATH];
        GetModuleFileNameA(m, p, MAX_PATH);
        char *s = strrchr(p, '\\');
        wsprintfA(out, "%s+%X", s ? s + 1 : p, (unsigned)(a - (uintptr_t)m));
    }
}

static LONG CALLBACK veh(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    /* the serious ones: access violation, division by zero, illegal instruction, stack overflow, heap corruption,
       in-page error, privileged instruction, array bounds... (not C++ exceptions or debugger notices) */
    if ((code & 0xF0000000u) != 0xC0000000u || code == 0xE06D7363u) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&g_count) > 4) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = ep->ContextRecord;
    char m[MAX_PATH + 16];
    mod_of(c->Eip, m, sizeof m);
    onc_log("EXCEPTION %08lX at %08lX (%s), %s address %08lX", code, c->Eip, m,
            ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
            (unsigned long)ep->ExceptionRecord->ExceptionInformation[1]);
    onc_log("  eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX edi %08lX ebp %08lX esp %08lX",
            c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    uint32_t *sp = (uint32_t *)c->Esp;
    for (int i = 0; i < 48; i += 8) {
        if (IsBadReadPtr(sp + i, 32)) break;
        onc_log("  [esp+%02X] %08X %08X %08X %08X %08X %08X %08X %08X", i * 4, sp[i], sp[i + 1], sp[i + 2], sp[i + 3],
                sp[i + 4], sp[i + 5], sp[i + 6], sp[i + 7]);
    }
    /* return addresses into Game.exe further up the stack */
    char line[512]; int n = 0;
    for (int i = 0; i < 512 && n < 400; i++) {
        if (IsBadReadPtr(sp + i, 4)) break;
        uint32_t v = sp[i];
        if (v > 0x401000 && v < 0x580000 && ((uint8_t *)v)[-5] == 0xE8) n += wsprintfA(line + n, " %08X", v);
    }
    line[n] = 0;
    onc_log("  calls:%s", line);
    return EXCEPTION_CONTINUE_SEARCH;
}

void onc_crash_init(void) {
    AddVectoredExceptionHandler(1, veh);
}
