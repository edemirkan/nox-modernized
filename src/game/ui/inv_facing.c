/* The player keeps his facing while the inventory is open.
   The client turns the player towards the cursor every frame (0x42F5A0: atan2 of the cursor around the screen centre
   -> 0x68B160, 0..255, sent to the server at 0x42F65D). With the inventory open the cursor is on the inventory, so the
   player turned towards it, and an item dropped (the pad's X drops it 28 units in front of the player along 0x68B160)
   landed on that side. Now, while the inventory is open, the facing is not recomputed when the pad is in use (its
   cursor is on the inventory all along), or with the mouse while the cursor is over the panel window [0x6D56CC]
   (the doll and the tray) or the tab strip above it (not the outer window [0x6D56C8]: its rectangle never holds it).
   The game then sends the facing the player had when the inventory opened. With the mouse out in the world
   (walking with the inventory open) the facing follows the cursor as usual.

   [inventory] keep_facing = 1 (0: as the original)

   Game.exe 0x42F5A0: "cmp byte [0x68991C], 2; je 0x42F65D" (13 bytes) -> jmp to our stub, which goes to 0x42F65D
   (send the stored facing) while locked, else does the same compare and goes on at 0x42F5AD. */
#include "inventory.h"
#include "gui.h"

static int g_on, g_logged;

int __cdecl onc_inv_facing_locked(void) {
    static int was_open;
    int lock = 0, open = g_on && ONC_I32(NOX_CLIENT_CONNECTED) && *(volatile uint8_t *)NOX_INV_STATE != 0;
    int mx = ONC_I32(NOX_MOUSE_X), my = ONC_I32(NOX_MOUSE_Y);
    onc_rect r = {0, 0, 0, 0};
    if (open) {
        /* with the pad the cursor is on the inventory all the time it is open (or going there): kept throughout;
           with the mouse: while the cursor is on the panel window (the doll and the tray) or the tab strip above */
        nox_win *panel = *(nox_win **)NOX_INV_PANEL;
        if (panel) r = onc_win_rect(panel);
        lock = onc_pad_in_use() || (panel && mx >= r.x0 && mx < r.x1 && my >= r.y0 - 24 && my < r.y1);
        if (!was_open && *(volatile uint8_t *)NOX_INV_STATE == 2) {
            was_open = 1;
            onc_log("inventory: open, panel %d,%d-%d,%d, cursor %d,%d", r.x0, r.y0, r.x1, r.y1, mx, my);
        }
    }
    if (!open) was_open = 0;
    if (lock != g_logged) { g_logged = lock; onc_log("inventory: facing %s", lock ? "kept" : "follows the cursor"); }
    return lock;
}

__asm__(".globl _onc_inv_facing_stub\n_onc_inv_facing_stub:\n"
        "  pushal\n  call _onc_inv_facing_locked\n  testl %eax, %eax\n  popal\n"
        "  jnz 1f\n"
        "  cmpb $2, 0x68991C\n  je 1f\n"
        "  pushl $0x42F5AD\n  ret\n"
        "1:\n  pushl $0x42F65D\n  ret\n");
void onc_inv_facing_stub(void);

void onc_inv_facing_init(void) {
    g_on = onc_cfg_int("inventory", "keep_facing", 1);
    if (!g_on) { onc_log("inventory: keep_facing off"); return; }
    /* cmp byte ptr [0x68991C], 2 ; je 0x42F65D */
    static const uint8_t orig[] = {0x80, 0x3D, 0x1C, 0x99, 0x68, 0x00, 0x02, 0x0F, 0x84, 0xB0, 0x00, 0x00, 0x00};
    if (onc_hook_jmp("inventory facing", 0x42F5A0, (void *)onc_inv_facing_stub, orig, sizeof orig))
        onc_log("hooked inventory facing (0042F5A0)");
    else g_on = 0;
}
