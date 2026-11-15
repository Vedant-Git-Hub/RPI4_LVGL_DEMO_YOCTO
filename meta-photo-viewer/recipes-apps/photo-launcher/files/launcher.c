#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <lvgl/lvgl.h>
#include <lvgl/src/drivers/evdev/lv_evdev.h>
#include "brightness_control.h"
#include "display_layout.h"

static lv_display_t *display;
static lv_obj_t *root;
static lv_indev_t *launcher_input;
static const char *const programs[] = {
    "/usr/bin/photo-camera", "/usr/bin/photo-images",
    "/usr/bin/photo-settings", "/usr/bin/photo-videos"
};

static void style(lv_obj_t *object, unsigned int color, int radius)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
}

static int axis_range(int fd, unsigned int mt, unsigned int ordinary,
                      struct input_absinfo *range)
{
    if(ioctl(fd, EVIOCGABS(mt), range) == 0) return 1;
    return ioctl(fd, EVIOCGABS(ordinary), range) == 0;
}

static lv_indev_t *enable_touch(void)
{
    DIR *directory = opendir("/dev/input");
    if(!directory) return NULL;
    struct dirent *entry;
    char mouse_path[128] = {0};
    while((entry = readdir(directory))) {
        if(strncmp(entry->d_name, "event", 5)) continue;
        char path[128], name[256] = {0};
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if(fd < 0) continue;
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        struct input_absinfo x, y;
        int ranged = axis_range(fd, ABS_MT_POSITION_X, ABS_X, &x) &&
                     axis_range(fd, ABS_MT_POSITION_Y, ABS_Y, &y) &&
                     x.maximum > x.minimum && y.maximum > y.minimum;
        close(fd);
        for(char *letter = name; *letter; letter++)
            if(*letter >= 'A' && *letter <= 'Z') *letter += 'a' - 'A';
        if(strstr(name, "touch") || strstr(name, "goodix") ||
           strstr(name, "ft5") || strstr(name, "edt")) {
            lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, path);
            if(touch) {
                if(ranged) lv_evdev_set_calibration(touch, x.minimum, y.minimum,
                                                    x.maximum, y.maximum);
                closedir(directory);
                return touch;
            }
        }
        if(!mouse_path[0] && strstr(name, "mouse"))
            snprintf(mouse_path, sizeof(mouse_path), "%s", path);
    }
    closedir(directory);
    if(mouse_path[0]) return lv_evdev_create(LV_INDEV_TYPE_POINTER, mouse_path);
    return NULL;
}

static void launch(lv_event_t *event)
{
    unsigned int index = (unsigned int)(uintptr_t)lv_event_get_user_data(event);
    if(index >= 4) return;
    /* This parent stops rendering while a child owns the framebuffer. */
    pid_t child = fork();
    if(child == 0) {
        execl(programs[index], programs[index], (char *)NULL);
        _exit(127);
    }
    if(child < 0) return;
    int status;
    while(waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    /* The parent's evdev fd accumulated all touch events from the child.
     * Reopen it so a stale tap cannot immediately relaunch an app. */
    if(launcher_input) lv_evdev_delete(launcher_input);
    launcher_input = enable_touch();
    lv_indev_reset(NULL, NULL);
    lv_obj_invalidate(root);
    lv_refr_now(display);
}

static void add_app(lv_obj_t *parent, int index, const char *title,
                    const char *symbol, unsigned int color)
{
    int group_width = PHOTO_SCREEN_WIDTH == 1024 ? 190 : 165;
    int gap = PHOTO_SCREEN_WIDTH == 1024 ? 38 : 23;
    int x = (PHOTO_SCREEN_WIDTH - 4 * group_width - 3 * gap) / 2 +
            index * (group_width + gap);
    int y = (PHOTO_SCREEN_HEIGHT - 155) / 2;
    lv_obj_t *app = lv_button_create(parent);
    lv_obj_set_pos(app, x, y);
    lv_obj_set_size(app, group_width, 150);
    style(app, 0x08151C, 0);
    lv_obj_set_style_bg_opa(app, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(app, 0, 0);
    lv_obj_t *icon = lv_obj_create(app);
    lv_obj_set_size(icon, 92, 92);
    lv_obj_set_pos(icon, (group_width - 92) / 2, 7);
    style(icon, color, 22);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    if(index == 0) {
        lv_obj_t *body = lv_obj_create(icon);
        lv_obj_set_pos(body, 19, 30);
        lv_obj_set_size(body, 54, 40);
        style(body, 0xFFFFFF, 6);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *lens = lv_obj_create(body);
        lv_obj_set_size(lens, 21, 21);
        style(lens, color, 11);
        lv_obj_center(lens);
        lv_obj_clear_flag(lens, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *top = lv_obj_create(icon);
        lv_obj_set_pos(top, 28, 23);
        lv_obj_set_size(top, 22, 10);
        style(top, 0xFFFFFF, 3);
        lv_obj_clear_flag(top, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_t *glyph = lv_label_create(icon);
        lv_label_set_text(glyph, symbol);
        lv_obj_set_style_text_color(glyph, lv_color_white(), 0);
        lv_obj_center(glyph);
        lv_obj_set_style_transform_pivot_x(glyph, 7, 0);
        lv_obj_set_style_transform_pivot_y(glyph, 7, 0);
        lv_obj_set_style_transform_scale(glyph, 768, 0);
    }
    lv_obj_t *caption = lv_label_create(app);
    lv_label_set_text(caption, title);
    lv_obj_set_width(caption, group_width);
    lv_obj_set_pos(caption, 0, 113);
    lv_obj_set_style_text_color(caption, lv_color_white(), 0);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(app, launch, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
}

int main(void)
{
    if(!brightness_restore())
        fprintf(stderr, "photo-viewer: could not restore display brightness\n");
    lv_init();
    display = lv_linux_fbdev_create();
    if(!display) return 1;
    lv_linux_fbdev_set_file(display, "/dev/fb0");
    launcher_input = enable_touch();
    lv_obj_t *screen = lv_screen_active();
    style(screen, 0x000000, 0);
    root = lv_obj_create(screen);
    lv_obj_set_size(root, PHOTO_SCREEN_WIDTH, PHOTO_SCREEN_HEIGHT);
    style(root, 0x08151C, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    int width = lv_display_get_horizontal_resolution(display);
    int height = lv_display_get_vertical_resolution(display);
    int sx = width * 256 / PHOTO_SCREEN_WIDTH;
    int sy = height * 256 / PHOTO_SCREEN_HEIGHT;
    int scale = sx < sy ? sx : sy;
    if(scale < 1) scale = 1;
    lv_obj_set_pos(root, (width - PHOTO_SCREEN_WIDTH * scale / 256) / 2,
                   (height - PHOTO_SCREEN_HEIGHT * scale / 256) / 2);
    lv_obj_set_style_transform_pivot_x(root, 0, 0);
    lv_obj_set_style_transform_pivot_y(root, 0, 0);
    lv_obj_set_style_transform_scale(root, scale, 0);
    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Stable_V3.0");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFDD835), 0);
    lv_obj_set_pos(title, 28, 23);
    add_app(root, 0, "Camera", LV_SYMBOL_VIDEO, 0x85389B);
    add_app(root, 1, "Image Viewer", LV_SYMBOL_IMAGE, 0x008E6A);
    add_app(root, 2, "Settings", LV_SYMBOL_SETTINGS, 0x8A541A);
    add_app(root, 3, "Video Player", LV_SYMBOL_VIDEO, 0x175CA8);
    lv_refr_now(display);
    for(;;) { lv_timer_handler(); usleep(5000); }
}
