/* Gamepad book of knowledge (L1 + Select opens / closes it):
   - R1 switches between the spells (abilities) and the creatures tab;
   - the list: D-pad / left stick move over the entries (left / right switch the column and turn the pages),
     A opens the entry's page, B closes the book;
   - an entry's page: left / right turn to the previous / next entry, B goes back to the list;
     A on a spell (an ability, a conjurer's summon spell) carries it to the spell bar:
     left / right choose the slot, up / down the spell set, A puts it there, B cancels;
   - L1 + A on a spell carries it to the trap tray instead (wizard / conjurer): left / right the slot,
     L1 / R1 (or up / down) the trap set, A puts it there, B cancels. */
#pragma once
#include "gui.h"

typedef struct { int dir_x, dir_y, a, l1, b, r1, l1_press, back; } onc_book_input;
typedef struct { int put_cursor, x, y; } onc_book_output;

int  onc_book_active(void);
void onc_book_update(const onc_book_input *in, onc_book_output *out);
void onc_book_draw(void); /* highlight and hints, after the GUI */
