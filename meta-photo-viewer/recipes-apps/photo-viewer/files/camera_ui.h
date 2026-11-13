#ifndef CAMERA_UI_H
#define CAMERA_UI_H

#include <lvgl/lvgl.h>

void camera_ui_build(lv_obj_t *root, const char *photo_directory,
                     void (*on_back)(void), void (*on_saved)(const char *name),
                     void (*on_settings_back)(void));
void camera_ui_open(void);
void camera_ui_open_settings(void);
void camera_ui_close(void);
int camera_ui_is_open(void);

#endif
