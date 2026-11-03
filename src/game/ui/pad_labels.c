/* While the pad is in use, the HUD's key labels (spell / ability slots, potions, weapon swap) show the pad's
   buttons instead of keyboard keys: the game asks one function for the key bound to an action (0x430250), which
   answers with the button while the pad is used. The potion labels are copied once by the game (0x476CA0), so
   they are refreshed when the pad / mouse switch happens. The slot labels are drawn centred under the slot
   (the game draws them left aligned in a 10 pixel window: "L+Y" would lean right). */
#include "onc.h"
#include "gui.h"

#define FN_KEY_NAME      0x430250 /* wchar *keyName(action, index) */
#define FN_SLOT_LABEL    0x45F120 /* the slot label window's draw proc */
#define FN_POTION_LABELS 0x476CA0 /* copies the potion keys into the HUD (3 characters) */
#define FN_GET_POS       0x488370 /* window screen position (win, &x, &y) */
#define FN_TEXT_COLOR    0x435D00
#define FN_TEXT_SIZE     0x43F3C0
#define FN_TEXT          0x43F260
#define NOX_SLOT_FONT    0x6D24E8
#define NOX_WHITE        0x954794

const wchar_t *onc_bind_label(int action_event); /* game/input/binds.c: follows the class mapping */

static const wchar_t *(__cdecl *g_key_name)(int action, int index);

/* Not for the Input Config screen: it fills its list with these names (0x4C5646, 0x4CAFA6) and, on leaving,
   rebuilds the whole key configuration from the list's text (title -> key) and saves it to nox.cfg; a pad label
   there ("DR", "L+X") is no key, so that binding was dropped (the map key lost after visiting the screen). */
static int from_input_config(uintptr_t ret) {
    return (ret >= 0x4C5600 && ret < 0x4C5700) || (ret >= 0x4CAF00 && ret < 0x4CB000);
}

static const wchar_t *__cdecl key_name(int action, int index) {
    if (onc_pad_in_use() && index == 1 && !from_input_config((uintptr_t)__builtin_return_address(0))) {
        /* the potions: no label with the pad (the consumables wheel's combo is shown next to them) */
        if (action == 0x24 || action == 0x25 || action == 0x26) return L" ";
        const wchar_t *l = onc_bind_label(action);
        if (l) return l;
    }
    return g_key_name(action, index);
}

static int __cdecl slot_label(nox_win *w, void *dd) {
    (void)dd;
    int x = 0, y = 0;
    ((void (__cdecl *)(nox_win *, int *, int *))FN_GET_POS)(w, &x, &y);
    int action = *(int32_t *)((char *)w + 0x170) + 0x1C;
    const wchar_t *s = key_name(action, 1);
    if (s) {
        void *font = *(void **)NOX_SLOT_FONT;
        int tw = 0, th = 0;
        ((int (__cdecl *)(void *, const wchar_t *, int *, int *, int))FN_TEXT_SIZE)(font, s, &tw, &th, 0);
        ((void (__cdecl *)(int))FN_TEXT_COLOR)(ONC_I32(NOX_WHITE));
        ((int (__cdecl *)(void *, const wchar_t *, int, int))FN_TEXT)(font, s, x + 5 - tw / 2, y);
    }
    return 1;
}

void onc_labels_init(void) {
    if (!onc_cfg_int("pad", "button_labels", 1)) return;
    static const uint8_t kn[] = {0xA1, 0x5C, 0xB1, 0x68, 0x00}; /* mov eax, [key bindings] */
    g_key_name = (const wchar_t *(__cdecl *)(int, int))onc_hook_detour("key names", FN_KEY_NAME, key_name, kn, 5);
    if (!g_key_name) return;
    static const uint8_t sl[] = {0x51, 0x8B, 0x44, 0x24, 0x08};
    onc_hook_jmp("slot labels", FN_SLOT_LABEL, slot_label, sl, 5);
}

/* every frame (GUI draw): the potion labels follow a pad / mouse switch */
int onc_player_class(void);
void onc_labels_frame(void) {
    static int last = -1;
    int now = onc_pad_in_use() * 4 + onc_player_class() + 1; /* pad / mouse and the class */
    if (now == last || !g_key_name) return;
    last = now;
    if (ONC_I32(NOX_CLIENT_CONNECTED) && *(void **)0x96B8D0) ((void (__cdecl *)(void))FN_POTION_LABELS)();
}
