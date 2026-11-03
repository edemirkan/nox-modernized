#include "onc.h"

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res) {
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        char exe[MAX_PATH], cwd[MAX_PATH];
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        GetCurrentDirectoryA(MAX_PATH, cwd);
        onc_log("Nox Modernized %s", ONC_VERSION);
        onc_log("exe: %s", exe);
        onc_log("working dir (game data): %s", cwd);
        const char *onc_cfg_name(void);
        onc_log("settings: %s", onc_cfg_name());
        int onc_mss_load(void), onc_dinput_hook(void);
        if (!onc_mss_load()) return TRUE;
        /* each module reads its settings and installs its hooks; the order matters where noted */
        void onc_crash_init(void), onc_workers_init(void), onc_profile_init(void);
        void onc_light_init(void), onc_renderfx_init(void), onc_spritefx_init(void), onc_camera_init(void);
        void onc_resolution_init(void), onc_uisplit_init(void), onc_hfr_init(void), onc_cursor_init(void);
        void onc_tabs_init(void), onc_autosave_init(void), onc_carry_init(void), onc_fixes_init(void);
        void onc_targets_init(void), onc_labels_init(void), onc_hints_init(void), onc_radial_init(void);
        onc_crash_init();
        onc_workers_init();     /* helper threads, used by the smoothing and the split rendering */
        onc_profile_init();     /* for development only ([display] profile) */
        /* graphics */
        onc_light_init();
        onc_renderfx_init();
        onc_spritefx_init();
        onc_camera_init();
        onc_resolution_init();
        onc_uisplit_init();     /* after the resolution, which sets the split up */
        onc_hfr_init();
        onc_cursor_init();
        { void onc_awake_init(void); onc_awake_init(); } /* no screen saver while the game is in front */
        /* input and interface */
        onc_dinput_hook();
        onc_tabs_init();
        { void onc_inv_carry_init(void); onc_inv_carry_init(); } /* the inventory's drop at the cursor: UI scale / zoom */
        { void onc_inv_facing_init(void); onc_inv_facing_init(); } /* the player keeps his facing in the inventory */
        onc_targets_init();
        { void onc_ally_cast_init(void); onc_ally_cast_init(); }
        { void onc_aimcone_init(void); onc_aimcone_init(); }
        onc_labels_init();
        onc_hints_init();
        onc_radial_init();
        onc_pad_init();
        /* game */
        onc_autosave_init();
        onc_carry_init();
        onc_fixes_init();
    } else if (reason == DLL_PROCESS_DETACH) {
        onc_log("Game exited.");
    }
    return TRUE;
}
