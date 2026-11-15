#ifndef VIDEO_UI_H
#define VIDEO_UI_H

#include <lvgl/lvgl.h>

void video_ui_build(lv_obj_t *root, const char *directory,
                    void (*on_back)(void), void (*on_settings_back)(void));
void video_ui_open(void);
void video_ui_close(void);
void video_ui_rescan(void);
int video_ui_is_open(void);
int video_ui_has_selection(void);
void video_ui_open_settings(void);

#endif
