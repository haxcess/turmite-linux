#ifndef TURMITE_BOARD_7B_H
#define TURMITE_BOARD_7B_H
#include <stdbool.h>
#include <stdint.h>
#define BOARD_WIDTH 1024
#define BOARD_HEIGHT 600
void board_init(void);
void board_present(const uint16_t *pixels);
/* False means no new report or a bus error; leave the caller's state alone. */
bool board_touch_report(bool *pressed);
#endif
