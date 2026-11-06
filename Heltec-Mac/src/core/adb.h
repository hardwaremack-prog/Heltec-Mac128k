#ifndef ADB_H
#define ADB_H
/* Apple Desktop Bus for the Mac SE (see adb.c) */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void     adb_reset(void);
void     adb_set_state(int st, uint64_t now_us);   /* the Mac changed PB4/PB5 */
void     adb_tick(uint64_t now_us);                /* run a due state change */
uint64_t adb_next_due(void);                       /* when the next one is due (0 = none) */
void     adb_sixtieth(void);                       /* 60 Hz, before the VBL */
int      adb_int_line(void);                       /* PB3 level */

/* input: ADB key codes (0x00-0x7F), mouse button */
void     adb_key(uint8_t code, int down);
void     adb_mouse_button(int down);
int      adb_room(void);

#ifdef __cplusplus
}
#endif
#endif
