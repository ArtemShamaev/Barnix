#ifndef SYSTEM_SOUND_H
#define SYSTEM_SOUND_H
/* Shell sounds: a chime after login and a short tone for errors. The user's
 * useretc/sounds.cfg ("off") silences them; settings_load reads it. */
enum { SYSTEM_SOUND_START, SYSTEM_SOUND_ERROR, SYSTEM_SOUND_KEYMOUSE_ON, SYSTEM_SOUND_KEYMOUSE_OFF };
void system_sound(int which);
void system_sound_enable(int on);
int system_sound_enabled(void);
#endif
