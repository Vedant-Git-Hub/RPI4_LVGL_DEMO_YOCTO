#ifndef PHOTO_BRIGHTNESS_CONTROL_H
#define PHOTO_BRIGHTNESS_CONTROL_H

/* Return -1 when the display has no writable Linux backlight. */
int brightness_get_percent(void);
int brightness_set_percent(int percent);

/* A missing or invalid saved level returns -1. */
int brightness_load_saved(void);
int brightness_save(int percent);
int brightness_restore(void);

#endif
