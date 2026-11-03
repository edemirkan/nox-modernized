/* How we get into the game: a proxy mss32.dll (the Miles Sound System, which Game.exe imports). Game.exe loads
   DLLs from its own folder first, so it finds ours; every Miles call is forwarded to the real mss32.dll in the
   game folder (the working directory) through the generated thunks in mss32_thunks.S.
   Unlike dinput.dll, Wine has no built-in mss32.dll, so no DLL override is needed (Winlator). */
#include "onc.h"
#include "mss32_names.h"
#include <string.h>

void *onc_mss_ptrs[ONC_MSS_COUNT];

static void missing_export(void) {
    onc_log("FATAL: a Miles Sound System function was called that the real mss32.dll does not have");
    ExitProcess(1);
}

int onc_mss_load(void) {
    char path[MAX_PATH], self[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, path);
    strncat(path, "\\mss32.dll", MAX_PATH - strlen(path) - 1);
    GetModuleFileNameA(NULL, self, MAX_PATH);
    char *s = strrchr(self, '\\');
    if (s) strcpy(s + 1, "mss32.dll");
    if (lstrcmpiA(path, self) == 0) {
        /* started from the game folder itself: the real DLL must be next to us under another name */
        if (s) strcpy(s + 1, "mss32_real.dll");
        strcpy(path, self);
    }
    HMODULE real = LoadLibraryA(path);
    if (!real) {
        onc_log("FATAL: cannot load the real Miles DLL %s (error %lu) - start the game with the game folder as the working directory",
                path, GetLastError());
        MessageBoxA(NULL, "Nox Modernized: the game's mss32.dll was not found.\nStart the game with Launch.bat (it sets the Nox folder as the working directory).",
                    "Nox Modernized", MB_ICONERROR);
        return 0;
    }
    int missing = 0;
    for (int i = 0; i < ONC_MSS_COUNT; i++) {
        onc_mss_ptrs[i] = (void *)GetProcAddress(real, onc_mss_names[i]);
        if (!onc_mss_ptrs[i]) { onc_mss_ptrs[i] = (void *)missing_export; missing++; }
    }
    onc_log("Miles: %s loaded (%d functions forwarded, %d missing)", path, ONC_MSS_COUNT - missing, missing);
    return 1;
}
