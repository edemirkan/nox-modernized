#include "gui.h"

int onc_win_alive(nox_win *w) {
    return w && !(NOXW_FLAGS(w) & NOXS_DESTROYED);
}

int onc_win_visible(nox_win *w) {
    for (nox_win *it = w; it; it = NOXW_PARENT(it))
        if (NOXW_FLAGS(it) & (NOXS_HIDDEN | NOXS_DESTROYED)) return 0;
    return w != NULL;
}

onc_rect onc_win_rect(nox_win *w) {
    int x = 0, y = 0;
    for (nox_win *it = w; it; it = NOXW_PARENT(it)) { x += NOXW_X(it); y += NOXW_Y(it); }
    onc_rect r = {x, y, x + NOXW_W(w), y + NOXW_H(w)};
    return r;
}

int onc_win_key(nox_win *w, int dik) {
    nox_winproc f = (nox_winproc)NOXW_FUNC93(w);
    if (!f) return 0;
    int handled = f(w, NOXE_KEY, (uintptr_t)dik, 2) != 0;
    if (onc_win_alive(w) && NOXW_FUNC93(w) == (void *)f) handled |= f(w, NOXE_KEY, (uintptr_t)dik, 1) != 0;
    return handled;
}

void onc_win_press(nox_win *w) {
    /* the button's own keyboard handling (Enter); if it has none, the click notification its parent gets */
    if (onc_win_key(w, 0x1C)) return;
    nox_win *p = NOXW_DRAWWIN(w);
    nox_winproc f94 = p ? (nox_winproc)NOXW_FUNC94(p) : NULL;
    if (f94) f94(p, 0x4007, (uintptr_t)w, 0);
}
