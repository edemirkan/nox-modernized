/* Keeps the screen saver and the monitor's power saving off while the game is in front.
   Windows counts only the keyboard and mouse as activity, not a gamepad (XInput), so the screen saver could start in
   the middle of a game played with the pad, or during a long movie. The original game ran in exclusive full screen,
   which kept it away; our borderless window does not. The screen saver starting during the ending movie froze
   the game (the movie's last sound looping, "not responding").

   [display] keep_screen_on = 1 (0: Windows decides as usual)

   A small thread looks once a second whether a window of this process is in front; while it is, the thread holds a
   display request (SetThreadExecutionState, ES_DISPLAY_REQUIRED: no screen saver, no monitor off, no sleep). Behind
   another window (Alt+Tab) the request is dropped. Our ddraw.dll also turns down the screen saver / monitor off
   system commands sent to the game window (same setting). */
#include "onc.h"

static DWORD WINAPI awake_thread(LPVOID arg) {
    (void)arg;
    int held = 0;
    for (;;) {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        int front = fg && pid == GetCurrentProcessId() && !IsIconic(fg);
        if (front != held) {
            SetThreadExecutionState(front ? (ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS);
            held = front;
            onc_log("keep screen on: %s", front ? "game in front, screen saver held off" : "game in the background");
        }
        Sleep(1000);
    }
    return 0;
}

void onc_awake_init(void) {
    if (!onc_cfg_int("display", "keep_screen_on", 1)) { onc_log("keep screen on: off (nox-modernized.ini)"); return; }
    HANDLE t = CreateThread(NULL, 0, awake_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
