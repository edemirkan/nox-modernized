#include "book.h"
#include "draw.h"

/* Game.exe book of knowledge (OpenNox GAME2.c book functions, BSS - 0x2500) */
#define NOX_BOOK_WIN     0x6D19E0 /* the book window */
#define NOX_BOOK_LIST    0x5A4FB4 /* 1: the list is shown, 0: an entry's page */
#define NOX_BOOK_TAB     0x6D19EC /* 1: creatures */
#define NOX_BOOK_ITEM    0x6D1A28 /* the shown entry */
#define NOX_BOOK_PAGE    0x6D1A2C /* the list page */
#define NOX_BOOK_PAGES   0x6D1A30
#define NOX_BOOK_COUNT   0x6D1C68 /* entries = [COUNT] - [COUNT0] */
#define NOX_BOOK_COUNT0  0x6D1C6C
#define NOX_BOOK_PLAYER  0x6D1C70 /* player info (+0x8CB class, +4232 summons allowed) */
#define NOX_BOOK_SPELLS  0x6D1A44 /* the entries' spells */
#define NOX_BOOK_ANIM    0x6D19E8 /* page turning state */
#define NOX_BOOK_PAGEWIN 0x6D1A24 /* +96: its click handler */
#define NOX_BOOK_ICONWIN 0x6D1A3C
#define NOX_BOOK_BACK    0x6D1A34 /* the arrows */
#define NOX_BOOK_FWD     0x6D1A38
#define FN_BOOK_TOGGLE   0x45A7C0
#define FN_BOOK_BACK     0x45AD60 /* arrow procs (win, 5 = click) */
#define FN_BOOK_FWD      0x45ABC0
#define FN_BOOK_TABPROC  0x45AEB0 /* tab buttons 1310 spells / 1320 creatures (win, 5) */
#define FN_BOOK_OPEN     0x45B480 /* open entry i */
#define FN_CLICK_SPELL   0x45AD40
#define FN_CLICK_CREAT   0x45AD50
#define FN_MOUSE_SEQ     0x4324B0
#define FN_SPELL_FLAGS   0x424610 /* spellHasFlags(spell, flags) */
#define FN_GAME_FLAG     0x40A5C0
#define FN_SET_HIDDEN    0x488510
#define FN_CHILD_BY_ID   0x4889D0
#define FN_FONT_HEIGHT   0x43EEA0
#define FN_SOUND         0x4528D0
#define FN_SPELL_DROP    0x45D7F0 /* drop(spell, 0, x, y, 0): put a spell on the bar / tray slot at a point */
#define FN_ROW_GET       0x45DCD0
#define FN_ROW_SET       0x45DC60
#define NOX_SPELLBAR     0x5A54F8 /* -> the spell bar: +212 the 5 slot windows */
#define NOX_TRAP_STATE   0x6D2420 /* 1: the trap tray is shown */
#define NOX_TRAP_BUTTON  0x6D2430 /* the "trap" button (+4 flags & 8: enabled) */
#define NOX_TRAP_SLOTS   0x6D1EEC /* the tray's 3 slot windows */
#define FN_TRAP_TOGGLE   0x460BB0
#define FN_TRAP_PREV     0x45FF40
#define FN_TRAP_NEXT     0x45FEF0
#define NOX_PLAYER_INFO  0x96B8D0

static struct { int sel, last_tab, placing, trap, spell, slot, tslot, page; DWORD page_at; } bp = {.page = -2};

static void sound(int id) { ((void (__cdecl *)(int, int))FN_SOUND)(id, 100); }
static nox_win *book(void) { return *(nox_win **)NOX_BOOK_WIN; }
static int tab(void) { return ONC_I32(NOX_BOOK_TAB) != 0; }
static int list(void) { return ONC_I32(NOX_BOOK_LIST) != 0; }
static int count(void) { return ONC_I32(NOX_BOOK_COUNT) - ONC_I32(NOX_BOOK_COUNT0); }
static int lineh(void) { return ((int (__cdecl *)(void *))FN_FONT_HEIGHT)(NULL) + 2; }
static int rows(void) { int r = 141 / lineh() - 1; return r < 1 ? 1 : r; }

int onc_book_active(void) {
    if (!ONC_I32(NOX_CLIENT_CONNECTED)) return 0;
    nox_win *w = book();
    if (!w || !onc_win_visible(w)) { bp.placing = 0; return 0; }
    void **modal = *(void ***)NOX_GUI_MODAL;
    return !(modal && modal[0] && onc_win_visible((nox_win *)modal[0]));
}

static int trap_available(void) {
    void *info = *(void **)NOX_PLAYER_INFO;
    char *btn = *(char **)NOX_TRAP_BUTTON;
    if (!info || !btn) return 0;
    int cl = *(uint8_t *)((char *)info + 0x8CB);
    return (cl == 1 || cl == 2) && (*(uint32_t *)(btn + 4) & 8);
}

/* the spell (ability, summon spell) of the shown page that can be put on the bar, or 0 */
static int page_spell(void) {
    char *info = *(char **)NOX_BOOK_PLAYER;
    if (list() || !info) return 0;
    int cl = *(uint8_t *)(info + 0x8CB);
    int sp = ((int32_t *)NOX_BOOK_SPELLS)[ONC_I32(NOX_BOOK_ITEM)];
    if (!sp) return 0;
    if (!tab()) {
        if (cl == 0) return sp; /* a warrior's ability */
        return ((int (__cdecl *)(int, int))FN_SPELL_FLAGS)(sp, 0x15000) ? 0 : sp;
    }
    int (__cdecl *flag)(int) = (int (__cdecl *)(int))FN_GAME_FLAG;
    if (cl == 2 && (flag(0x2000) || flag(0x1000) || *(int32_t *)(info + 4232))) return sp + 74; /* summon */
    return 0;
}

static onc_rect win_rect_of(nox_win *w) { return onc_win_rect(w); }
static onc_rect entry_rect(int i) {
    onc_rect o = win_rect_of(book());
    int r = rows(), lh = lineh();
    int local = i - ONC_I32(NOX_BOOK_PAGE) * 2 * r, col = local / r, row = local % r;
    int cx = o.x0 + (col > 0 ? 199 : 78), y = o.y0 + 19 + row * lh;
    onc_rect rc = {cx - 58, y - 1, cx + 58, y + lh};
    return rc;
}
static onc_rect icon_rect(void) {
    onc_rect o = win_rect_of(book());
    onc_rect rc = {o.x0 + 63, o.y0 + 19, o.x0 + 93, o.y0 + 49};
    return rc;
}
static int place_rect(onc_rect *r) {
    nox_win *w;
    if (bp.trap) w = ((nox_win **)NOX_TRAP_SLOTS)[bp.tslot];
    else {
        char *bar = *(char **)NOX_SPELLBAR;
        w = bar ? ((nox_win **)(bar + 212))[bp.slot] : NULL;
    }
    if (!w) return 0;
    *r = onc_win_rect(w);
    return 1;
}
static void centre(onc_rect r, onc_book_output *o) { o->x = (r.x0 + r.x1) / 2; o->y = (r.y0 + r.y1) / 2; o->put_cursor = 1; }

static void arrow(int d) {
    nox_win *w = *(nox_win **)(d < 0 ? NOX_BOOK_BACK : NOX_BOOK_FWD);
    if (w) ((int (__cdecl *)(nox_win *, int))(d < 0 ? FN_BOOK_BACK : FN_BOOK_FWD))(w, 5);
}

static void to_list(int page) {
    ONC_I32(NOX_BOOK_LIST) = 1;
    ONC_I32(NOX_BOOK_PAGE) = page;
    void (__cdecl *hide)(void *, int) = (void (__cdecl *)(void *, int))FN_SET_HIDDEN;
    hide(*(void **)NOX_BOOK_ICONWIN, 1);
    hide(*(void **)NOX_BOOK_BACK, 0);
    hide(*(void **)NOX_BOOK_FWD, 0);
    ONC_I32(NOX_BOOK_ANIM) = 3;
    char *pw = *(char **)NOX_BOOK_PAGEWIN;
    if (pw) {
        int32_t *h = *(int32_t **)(pw + 96);
        h[3] = ((int (__cdecl *)(void))FN_MOUSE_SEQ)();
        h[0] = tab() ? FN_CLICK_CREAT : FN_CLICK_SPELL;
    }
    sound(788);
}

static void set_tab(int creatures) {
    nox_win *t = ((nox_win *(__cdecl *)(nox_win *, int))FN_CHILD_BY_ID)(book(), creatures ? 1320 : 1310);
    if (t) ((int (__cdecl *)(nox_win *, int))FN_BOOK_TABPROC)(t, 5);
    bp.sel = 0;
}

static void navigate(int dx, int dy, onc_book_output *out) {
    if (bp.placing) {
        if (bp.trap) {
            if (dx) { bp.tslot += dx; if (bp.tslot < 0) bp.tslot = 0; if (bp.tslot > 2) bp.tslot = 2; sound(920); }
            if (dy) ((void (__cdecl *)(void))(dy < 0 ? FN_TRAP_PREV : FN_TRAP_NEXT))();
        } else {
            if (dx) { bp.slot += dx; if (bp.slot < 0) bp.slot = 0; if (bp.slot > 4) bp.slot = 4; sound(920); }
            if (dy) ((void (__cdecl *)(int))FN_ROW_SET)((((int (__cdecl *)(void))FN_ROW_GET)() + dy + 5) % 5);
        }
        onc_rect r;
        if (place_rect(&r)) centre(r, out);
        return;
    }
    if (!list()) {
        if (dx) arrow(dx);
        centre(icon_rect(), out);
        return;
    }
    int n_all = count();
    if (n_all <= 0) return;
    int r = rows(), per = 2 * r, first = ONC_I32(NOX_BOOK_PAGE) * per;
    int n = n_all - first < per ? n_all - first : per;
    if (bp.sel < first || bp.sel >= first + n) bp.sel = first;
    int local = bp.sel - first, col = local / r, row = local % r;
    if (dy) {
        local += dy;
        if (local < 0) local = 0;
        if (local > n - 1) local = n - 1;
    } else if (dx > 0) {
        if (col == 0 && n > r) local = local + r < n - 1 ? local + r : n - 1;
        else if (ONC_I32(NOX_BOOK_PAGE) + 1 < ONC_I32(NOX_BOOK_PAGES)) {
            arrow(+1); /* the next page, its left column */
            first = ONC_I32(NOX_BOOK_PAGE) * per;
            n = n_all - first < per ? n_all - first : per;
            local = row < n - 1 ? row : n - 1;
        }
    } else if (dx < 0) {
        if (col == 1) local -= r;
        else if (ONC_I32(NOX_BOOK_PAGE) > 0) {
            arrow(-1); /* the previous page, its right column */
            first = ONC_I32(NOX_BOOK_PAGE) * per;
            n = n_all - first < per ? n_all - first : per;
            local = r + row < n - 1 ? r + row : n - 1;
        }
    }
    bp.sel = first + (local < 0 ? 0 : local);
    centre(entry_rect(bp.sel), out);
}

void onc_book_update(const onc_book_input *in, onc_book_output *out) {
    out->put_cursor = 0;
    if (tab() != bp.last_tab) { bp.last_tab = tab(); bp.sel = 0; }
    if (in->back) { bp.placing = 0; ((void (__cdecl *)(void))FN_BOOK_TOGGLE)(); return; }
    if (in->r1 && !bp.placing) { set_tab(!tab()); navigate(0, 0, out); return; }
    if (bp.placing && bp.trap && (in->r1 || in->l1_press)) { /* L1 / R1: the trap set */
        ((void (__cdecl *)(void))(in->r1 ? FN_TRAP_NEXT : FN_TRAP_PREV))();
        return;
    }
    if (in->a) {
        if (bp.placing) {
            if (bp.trap && ONC_I32(NOX_TRAP_STATE) != 1) ((void (__cdecl *)(void))FN_TRAP_TOGGLE)(); /* shown */
            onc_rect r;
            if (place_rect(&r)) {
                onc_log("book: spell %d to %s slot %d", bp.spell, bp.trap ? "trap" : "bar", bp.trap ? bp.tslot : bp.slot);
                /* the 2nd argument is the slot's "cast on oneself" mark (+4 of the slot): the game sets it itself only
                   for a drag from the book (0x487420), from the spell's flags 0x600 (defensive / on oneself, 0x424610);
                   without it Blink, Haste, Shield... go to whatever is under the cursor (nothing: no effect) */
                int self = ((int (__cdecl *)(int, int))0x424610)(bp.spell, 0x600) != 0;
                ((int (__cdecl *)(int, int, int, int, int))FN_SPELL_DROP)(bp.spell, self, (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2, 0);
            }
            bp.placing = bp.trap = 0;
            centre(icon_rect(), out);
            return;
        }
        if (list()) {
            if (count() > 0) {
                int i = bp.sel < 0 ? 0 : bp.sel >= count() ? count() - 1 : bp.sel;
                ((int (__cdecl *)(int))FN_BOOK_OPEN)(i);
                centre(icon_rect(), out);
            }
            return;
        }
        int sp = page_spell();
        int trap = in->l1;
        if (!sp || (trap && !trap_available())) { sound(925); return; }
        if (trap && ONC_I32(NOX_TRAP_STATE) != 1) ((void (__cdecl *)(void))FN_TRAP_TOGGLE)();
        bp.placing = 1; bp.trap = trap; bp.spell = sp;
        sound(920);
        onc_rect r;
        if (place_rect(&r)) centre(r, out);
        return;
    }
    if (in->b) {
        if (bp.placing) { bp.placing = bp.trap = 0; sound(921); centre(icon_rect(), out); return; }
        if (!list()) {
            int item = ONC_I32(NOX_BOOK_ITEM);
            bp.sel = item;
            to_list(item / (2 * rows()));
            centre(entry_rect(item), out);
            return;
        }
        ((void (__cdecl *)(void))FN_BOOK_TOGGLE)(); /* close */
        return;
    }
    if (in->dir_x || in->dir_y) navigate(in->dir_x, in->dir_y, out);
}

/* ---- highlight and hints ---- */
static void hint_lines(const wchar_t **lines, int n, int x, int y) {
    int fh = 0, w = 0;
    onc_text_size(L"Ag", NULL, &fh);
    for (int i = 0; i < n; i++) { int tw = 0; onc_text_size(lines[i], &tw, NULL); if (tw > w) w = tw; }
    int h = n * (fh + 2) + 4;
    y -= h;
    int sw = ONC_I32(NOX_SCREEN_W), sh = ONC_I32(NOX_SCREEN_H);
    if (x > sw - w - 10) x = sw - w - 10;
    if (x < 2) x = 2;
    if (y > sh - h - 2) y = sh - h - 2;
    if (y < 2) y = 2;
    for (int k = 0; k < 2; k++) ((void (__cdecl *)(int, int, int, int))0x484850)(x, y, w + 8, h);
    onc_text_color(ONC_I32(NOX_COLOR_YELLOW));
    for (int i = 0; i < n; i++) onc_text(lines[i], x + 4, y + 2 + i * (fh + 2));
}

void onc_book_draw(void) {
    if (!onc_book_active() || !onc_pad_in_use()) return;
    onc_rect r;
    int have = 0;
    if (bp.placing) have = place_rect(&r);
    else if (list() && count() > 0) {
        int first = ONC_I32(NOX_BOOK_PAGE) * 2 * rows();
        if (bp.sel >= first && bp.sel < first + 2 * rows() && bp.sel < count()) { r = entry_rect(bp.sel); have = 1; }
    } else if (!list() && page_spell()) {
        r = icon_rect(); r.x0 -= 2; r.y0 -= 2; r.x1 += 2; r.y1 += 2; have = 1;
    }
    onc_draw_begin();
    if (have) {
        int c = (GetTickCount() / 375) % 2 ? ONC_I32(NOX_COLOR_YELLOW) : onc_rgb(255, 255, 255);
        onc_draw_frame(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0, c);
        if (bp.placing) onc_draw_frame(r.x0 + 1, r.y0 + 1, r.x1 - r.x0 - 2, r.y1 - r.y0 - 2, c);
    }
    if (bp.placing && have) {
        if (bp.trap) {
            const wchar_t *l[] = {L"A: put in this trap slot", L"Left / Right: slot", L"L1 / R1: trap set", L"B: cancel"};
            hint_lines(l, 4, r.x0, r.y0 - 6);
        } else {
            const wchar_t *l[] = {L"A: put here   B: cancel   Up / Down: spell set"};
            hint_lines(l, 1, r.x0, r.y0 - 6);
        }
    } else {
        /* a spell's page: what A and L1 + A do, shortly after it is opened, for a while */
        int page = list() ? -1 : ONC_I32(NOX_BOOK_ITEM) + (tab() ? 10000 : 0);
        DWORD now = GetTickCount();
        if (page != bp.page) { bp.page = page; bp.page_at = now; }
        if (page >= 0 && page_spell() && now - bp.page_at >= 500 && now - bp.page_at <= 7000) {
            onc_rect ic = icon_rect();
            const wchar_t *l[] = {L"A: put on the spell bar", L"L1 + A: put in a trap"};
            hint_lines(l, trap_available() ? 2 : 1, ic.x1 + 8, ic.y1);
        }
    }
    onc_draw_end();
}
