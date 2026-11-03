/* Rolling autosaves, so that an unlucky autosave never costs the only good one: every time the autosave slot is written (the game's own
   autosaves, the quick save, "Save to AutoSave"), the previous one is kept: AUTOSAVE -> AUTOSAV1 -> AUTOSAV2
   (the oldest is dropped). The names have 8 letters: the game takes a save's folder name from its path as the 8
   letters before the last backslash (0x4DC450), so a longer name such as AUTOSAVE_1 would read as TOSAVE_1.
   The save / load lists get two more rows for them, right under the autosave; they are labelled "(previous autosave)" / "(older autosave)", can be loaded, and cannot be saved over.

   [save] rolling_autosave = 1 (0: the original single autosave)

   Game.exe:
   - 0x4DAE50 saves a game: at 0x4DAFE4 it copies WORKING to the slot with 0x4DBBE0(from, to) (to = the slot name;
     the target folder is made by 0x4DB020). Save folders are <0x5D2618>\Save\<name>.
   - 0x48AEF0(list, names, styles, array) fills a save list: row 0 = AUTOSAVE, rows 1..13 = SAVE0001..SAVE0013
     (0x419EB0(path, entry) reads Player.plr into an entry, 0x4FE bytes; the path is at +4). The display loop
     (esi 0..13, 0x48B53E) writes the date / name / style columns; the colour comes from 0x48AE20(entry), called
     at 0x48B489, with the slot name at [esp+0x18] and the label (wide) at [esp+0x24] of the caller.
   - The in-game Save / Load dialog (0x48A9D0) keeps 14 entries at 0x6F7450 (cleared at 0x48B64D / 0x48B7A1, the
     path read at 0x6F7454 + row * 0x4FE); the variables after it (0x6FBA34...) leave no room: moved.
     Save on an empty row names the slot at 0x48AC1E (row 0 AUTOSAVE, else SAVE%04d row); on a used row the name
     comes from its path (0x48AB90).
   - The main menu's load list allocates 14 entries at 0x4A4503 (push 0xe); 0x48AE70 picks the newest of 14 rows. */
#include "onc.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define ROWS 16
#define ENTRY 0x4FE
static uint8_t g_arr[ROWS * ENTRY + 16];
static int g_on;

static const char *base_dir(void) { return (const char *)0x5D2618; }
static void save_path(char *out, int n, const char *name) { snprintf(out, n, "%s\\Save\\%s", base_dir(), name); }

/* the folder of a row of the save lists */
static void row_name(int row, char *out, int n) {
    if (row == 0) snprintf(out, n, "AUTOSAVE");
    else if (row == 1) snprintf(out, n, "AUTOSAV1");
    else if (row == 2) snprintf(out, n, "AUTOSAV2");
    else snprintf(out, n, "SAVE%04d", row - 2);
}

/* ---------------- rotation ---------------- */
static int exists(const char *name) {
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s\\Save\\%s\\Player.plr", base_dir(), name);
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

static void delete_tree(const char *dir) {
    char pat[MAX_PATH];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            char p[MAX_PATH];
            snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) delete_tree(p);
            else { SetFileAttributesA(p, FILE_ATTRIBUTE_NORMAL); DeleteFileA(p); }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir);
}

static int copy_tree(const char *from, const char *to) {
    CreateDirectoryA(to, NULL);
    char pat[MAX_PATH];
    snprintf(pat, sizeof pat, "%s\\*", from);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int ok = 1;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char a[MAX_PATH], b[MAX_PATH];
        snprintf(a, sizeof a, "%s\\%s", from, fd.cFileName);
        snprintf(b, sizeof b, "%s\\%s", to, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok &= copy_tree(a, b);
        else ok &= CopyFileA(a, b, FALSE) != 0;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return ok;
}

/* moves a save folder (a rename; a copy if that fails) */
static void move_save(const char *from, const char *to) {
    char a[MAX_PATH], b[MAX_PATH];
    save_path(a, sizeof a, from);
    save_path(b, sizeof b, to);
    delete_tree(b);
    if (MoveFileA(a, b)) return;
    if (copy_tree(a, b)) delete_tree(a);
    else onc_log("rolling autosave: could not move %s to %s (%lu)", from, to, GetLastError());
}

static void rotate(void) {
    if (!exists("AUTOSAVE")) return;
    if (exists("AUTOSAV1")) move_save("AUTOSAV1", "AUTOSAV2");
    move_save("AUTOSAVE", "AUTOSAV1");
    onc_log("rolling autosave: kept the previous autosaves");
}

/* 0x4DAFE4: 0x4DBBE0("WORKING", name) */
static int __cdecl save_copy(const char *from, const char *to) {
    if (g_on && to && !_stricmp(to, "AUTOSAVE")) rotate();
    return ((int (__cdecl *)(const char *, const char *))0x4DBBE0)(from, to);
}

/* ---------------- the lists ---------------- */
/* 0x48AFBF (after row 0 is read; ebx = the entries, ebp = the count so far): rows 1..15 */
int __cdecl onc_autosave_read_rows(uint8_t *arr, int count) {
    for (int row = 1; row < ROWS; row++) {
        char name[16], p[MAX_PATH];
        row_name(row, name, sizeof name);
        snprintf(p, sizeof p, "%s\\Save\\%s\\Player.plr", base_dir(), name);
        if (((int (__cdecl *)(const char *, void *))0x419EB0)(p, arr + row * ENTRY)) count++;
    }
    return count;
}
__asm__(".globl _onc_autosave_read_stub\n_onc_autosave_read_stub:\n"
        "  pushl %ebp\n  pushl %ebx\n  call _onc_autosave_read_rows\n  addl $8, %esp\n"
        "  movl %eax, %ebp\n"
        "  movl $0x48B007, %eax\n  jmp *%eax\n");
void onc_autosave_read_stub(void);

/* 0x48B05C: an empty row says "Empty Slot", except the autosave rows (0..2), which stay blank */
__asm__(".globl _onc_autosave_empty_stub\n_onc_autosave_empty_stub:\n"
        "  pushl $3\n  cmpl $3, %esi\n  jae 1f\n"
        "  movl $0x48B062, %eax\n  jmp *%eax\n"
        "1:\n  movl $0x48B09A, %eax\n  jmp *%eax\n");
void onc_autosave_empty_stub(void);

/* 0x48B489: 0x48AE20(entry) = the row colour; the history rows get the autosave colour and their label */
int __cdecl onc_autosave_colour(void *entry, uint8_t *caller_esp) {
    int c = ((int (__cdecl *)(void *))0x48AE20)(entry);
    const char *name = (const char *)(caller_esp + 0x18);
    wchar_t *label = (wchar_t *)(caller_esp + 0x24);
    const wchar_t *tag = !strcmp(name, "AUTOSAV1") ? L" (previous autosave)"
                       : !strcmp(name, "AUTOSAV2") ? L" (older autosave)" : NULL;
    if (tag && c != 10) {
        if (wcslen(label) + wcslen(tag) < 200) wcscat(label, tag);
        c = 13;
    }
    return c;
}
/* called in place of 0x48AE20: [esp+4] = entry, the caller's esp (before its push) = esp + 8 */
__asm__(".globl _onc_autosave_colour_stub\n_onc_autosave_colour_stub:\n"
        "  leal 8(%esp), %eax\n  pushl %eax\n  pushl 8(%esp)\n  call _onc_autosave_colour\n  addl $8, %esp\n  ret\n");
void onc_autosave_colour_stub(void);

/* 0x48AC1E: the slot of an empty row (edx = row; the name goes to [esp+8]) */
int __cdecl onc_autosave_slot(int row, char *out) {
    if (row == 1 || row == 2) {
        ((void (__cdecl *)(int, int))0x4528D0)(925, 100); /* the game's "can't do that" sound */
        return 0;
    }
    row_name(row, out, 12);
    return 1;
}
__asm__(".globl _onc_autosave_slot_stub\n_onc_autosave_slot_stub:\n"
        "  leal 8(%esp), %eax\n  pushl %eax\n  pushl %edx\n  call _onc_autosave_slot\n  addl $8, %esp\n"
        "  testl %eax, %eax\n  jz 1f\n"
        "  movl $0x48AC54, %eax\n  jmp *%eax\n"
        "1:\n  movl $0x48AC71, %eax\n  jmp *%eax\n");
void onc_autosave_slot_stub(void);

/* 0x48AB90: saving over a used row (edx = row): not over the autosave history */
int __cdecl onc_autosave_used_row(int row) {
    if (row == 1 || row == 2) { ((void (__cdecl *)(int, int))0x4528D0)(925, 100); return 0; }
    return 1;
}
__asm__(".globl _onc_autosave_used_stub\n_onc_autosave_used_stub:\n"
        "  pushl %edx\n  call _onc_autosave_used_row\n  popl %edx\n"
        "  testl %eax, %eax\n  jz 1f\n"
        "  leal (%edx,%edx,4), %eax\n  orl $-1, %ecx\n"
        "  pushl $0x48AB96\n  ret\n"
        "1:\n  movl $0x48AC71, %eax\n  jmp *%eax\n");
void onc_autosave_used_stub(void);

/* ---------------- install ---------------- */
static int patch(uint32_t a, const void *expect, const void *b, size_t n) {
    if (expect && memcmp((void *)a, expect, n)) return 0;
    DWORD old;
    VirtualProtect((void *)a, n, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)a, b, n);
    VirtualProtect((void *)a, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)a, n);
    return 1;
}
static int jmp_to(uint32_t a, const void *expect, size_t n, void *target) {
    uint8_t b[16];
    memset(b, 0x90, sizeof b);
    b[0] = 0xE9;
    int32_t rel = (int32_t)((uintptr_t)target - (a + 5));
    memcpy(b + 1, &rel, 4);
    return patch(a, expect, b, n);
}
static int imm32(uint32_t a, uint32_t expect, uint32_t v) { return patch(a, &expect, &v, 4); }

void onc_autosave_init(void) {
    g_on = onc_cfg_int("save", "rolling_autosave", 1) != 0;
    if (!g_on) { onc_log("rolling autosave: off"); return; }
    /* check every place first, then patch: all or nothing */
    static const uint8_t e_read[] = {0xBE, 0x01, 0x00, 0x00, 0x00};          /* 48AFBF mov esi, 1 */
    static const uint8_t e_empty[] = {0x85, 0xF6, 0x6A, 0x03, 0x75, 0x38};   /* 48B05C test esi,esi; push 3; jne */
    static const uint8_t e_slot[] = {0x85, 0xD2, 0x75, 0x1F, 0xA1};          /* 48AC1E test edx,edx; jne; mov eax */
    static const uint8_t e_used[] = {0x8D, 0x04, 0x92, 0x83, 0xC9, 0xFF};    /* 48AB90 lea eax,[edx+edx*4]; or ecx,-1 */
    struct { uint32_t a; uint32_t v; } imms[] = {
        {0x48B64E, 0x1179}, {0x48B653, 0x6F7450}, {0x48B658, 0x6F7450}, /* clear + fill (in-game) */
        {0x48B7A2, 0x1179}, {0x48B7A9, 0x6F7450},                       /* clear */
        {0x48AAC1, 0x6F7454}, {0x48AB9E, 0x6F7454}, {0x48AC9C, 0x6F7454}, /* path of the selected row */
    };
    int ok = !memcmp((void *)0x48AFBF, e_read, 5) && !memcmp((void *)0x48B05C, e_empty, 6) &&
             !memcmp((void *)0x48AC1E, e_slot, 5) && !memcmp((void *)0x48AB90, e_used, 6) &&
             *(uint8_t *)0x48B540 == 0x0E && *(uint8_t *)0x4A4504 == 0x0E && *(uint8_t *)0x48AED9 == 0x0E;
    for (size_t i = 0; ok && i < sizeof imms / sizeof imms[0]; i++) ok = *(uint32_t *)imms[i].a == imms[i].v;
    if (!ok) { onc_log("rolling autosave: Game.exe differs, not installed"); g_on = 0; return; }

    uint32_t arr = (uint32_t)(uintptr_t)g_arr;
    uint32_t dwords = ROWS * ENTRY / 4;
    imm32(0x48B64E, 0x1179, dwords); imm32(0x48B653, 0x6F7450, arr); imm32(0x48B658, 0x6F7450, arr);
    imm32(0x48B7A2, 0x1179, dwords); imm32(0x48B7A9, 0x6F7450, arr);
    imm32(0x48AAC1, 0x6F7454, arr + 4); imm32(0x48AB9E, 0x6F7454, arr + 4); imm32(0x48AC9C, 0x6F7454, arr + 4);
    uint8_t rows = ROWS;
    patch(0x48B540, NULL, &rows, 1); /* the display loop */
    patch(0x4A4504, NULL, &rows, 1); /* the main menu's load list */
    patch(0x48AED9, NULL, &rows, 1); /* 0x48AE70: the newest save gets selected */
    jmp_to(0x48AFBF, e_read, 5, onc_autosave_read_stub);
    jmp_to(0x48B05C, e_empty, 6, onc_autosave_empty_stub);
    jmp_to(0x48AC1E, e_slot, 5, onc_autosave_slot_stub);
    jmp_to(0x48AB90, e_used, 6, onc_autosave_used_stub);
    onc_hook_call("rolling autosave: list colour", 0x48B489, onc_autosave_colour_stub, 0x48AE20);
    onc_hook_call("rolling autosave: save copy", 0x4DAFE4, save_copy, 0x4DBBE0);
    onc_log("rolling autosave: on (AUTOSAVE, AUTOSAV1, AUTOSAV2; %d rows)", ROWS);
}
