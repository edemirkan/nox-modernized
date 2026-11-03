/* The gamepad button combos (shared by the game and the settings tool): every button alone or with L1 held
   (L1 itself is the modifier). */
#pragma once

enum { /* the buttons, in the order the controls list shows them */
    C_A, C_B, C_X, C_Y, C_LT, C_RT, C_R1, C_L3, C_R3, C_SEL, C_START, C_DU, C_DD, C_DL, C_DR, C_BASE_N
};
#define S_N (2 * C_BASE_N)        /* combos: c alone, c + C_BASE_N with L1 */
#define S_L1(c) ((c) + C_BASE_N)
