#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include <lvgl/lvgl.h>
#include <lvgl/src/drivers/evdev/lv_evdev.h>

#include "media_server.h"
#include "image_convert.h"
#include "wifi_control.h"

#define PHOTO_DIR "/opt/photo-viewer/photos"
#define CARD_DEVICE "/dev/mmcblk0p1"
#define MAX_FILES 64
#define PREVIEW_WIDTH 440
#define PREVIEW_HEIGHT 264

typedef struct {
    char name[128];
    char path[384];
    off_t size;
} photo_file_t;

static photo_file_t files[MAX_FILES];
static int file_count;
static int selected = -1;
static int converted_index = -1;
static int card_mounted;
static char converted_path[128];
static char preview_path[128];
static char edit_preview_path[128];
static lv_timer_t *slideshow_timer;
static lv_obj_t *gallery, *viewer, *file_list, *preview_image, *full_image;
static lv_obj_t *preview_viewport, *preview_gif, *full_gif;
static lv_obj_t *selected_label, *count_label, *status_label;
static lv_obj_t *edit_button, *edit_screen, *edit_preview_image;
static lv_obj_t *edit_status_label, *edit_rotation_label;
static lv_obj_t *fit_button, *fill_button;
static image_settings_t editing_settings;
static lv_obj_t *storage_label;
static lv_obj_t *wifi_screen, *wifi_list, *wifi_ssid, *wifi_password;
static lv_obj_t *wifi_status_label, *wifi_keyboard;
static wifi_network_t wifi_networks[WIFI_MAX_NETWORKS];
static lv_timer_t *wifi_scan_timer;

static void style_box(lv_obj_t *obj, uint32_t color, int radius)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static lv_obj_t *text(lv_obj_t *parent, const char *value, uint32_t color, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, value);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *button(lv_obj_t *parent, const char *value, uint32_t color,
                        int x, int y, int width, int height)
{
    lv_obj_t *item = lv_button_create(parent);
    lv_obj_set_pos(item, x, y);
    lv_obj_set_size(item, width, height);
    style_box(item, color, 8);
    lv_obj_set_style_shadow_width(item, 0, 0);
    lv_obj_t *caption = lv_label_create(item);
    lv_label_set_text(caption, value);
    lv_obj_set_style_text_color(caption, lv_color_white(), 0);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
    lv_obj_set_width(caption, width - 12);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(caption);
    return item;
}

static void set_status(const char *message)
{
    lv_label_set_text(status_label, message);
}

static int refresh_card_mount(void)
{
    int present = access(CARD_DEVICE, F_OK) == 0;
    if(present && !card_mounted) {
        if(mount(CARD_DEVICE, PHOTO_DIR, "vfat", MS_NOATIME, "utf8=1") &&
           mount(CARD_DEVICE, PHOTO_DIR, "exfat", MS_NOATIME, NULL) &&
           mount(CARD_DEVICE, PHOTO_DIR, "ext4", MS_NOATIME, NULL)) return 0;
        card_mounted = 1;
        lv_label_set_text(storage_label, "SD");
        return 1;
    }
    if(!present && card_mounted) {
        if(umount2(PHOTO_DIR, MNT_DETACH)) return 0;
        card_mounted = 0;
        lv_label_set_text(storage_label, "PI");
        return 1;
    }
    return 0;
}

static int compare_files(const void *left, const void *right)
{
    const photo_file_t *a = left;
    const photo_file_t *b = right;
    return strcasecmp(a->name, b->name);
}

static void clear_converted(void)
{
    if(converted_path[0]) {
        char source[sizeof(converted_path) + 3];
        snprintf(source, sizeof(source), "A:%s", converted_path);
        lv_image_set_src(full_image, NULL);
        lv_image_cache_drop(source);
        unlink(converted_path);
        converted_path[0] = '\0';
    }
    if(preview_path[0]) {
        char source[sizeof(preview_path) + 3];
        snprintf(source, sizeof(source), "A:%s", preview_path);
        lv_image_set_src(preview_image, NULL);
        lv_image_cache_drop(source);
        unlink(preview_path);
        preview_path[0] = '\0';
    }
    converted_index = -1;
}

static void clear_gifs(void)
{
    if(preview_gif) { lv_obj_delete(preview_gif); preview_gif = NULL; }
    if(full_gif) { lv_obj_delete(full_gif); full_gif = NULL; }
    lv_obj_clear_flag(preview_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(full_image, LV_OBJ_FLAG_HIDDEN);
}

static int show_gif(lv_obj_t **object, lv_obj_t *parent, const char *path,
                    const image_settings_t *settings, int width, int height)
{
    char source[sizeof(files[0].path) + 3];
    snprintf(source, sizeof(source), "A:%s", path);
    if(!*object) *object = lv_gif_create(parent);
    lv_gif_set_src(*object, source);
    lv_gif_t *animation = (lv_gif_t *)*object;
    if(!animation->gif) {
        lv_obj_delete(*object);
        *object = NULL;
        return 0;
    }
    int original_width = animation->gif->width;
    int original_height = animation->gif->height;
    int rotated_width = settings->rotation % 180 ? original_height : original_width;
    int rotated_height = settings->rotation % 180 ? original_width : original_height;
    if(rotated_width <= 0 || rotated_height <= 0) {
        lv_obj_delete(*object);
        *object = NULL;
        return 0;
    }
    uint32_t xscale = (uint32_t)((int64_t)width * 256 / rotated_width);
    uint32_t yscale = (uint32_t)((int64_t)height * 256 / rotated_height);
    uint32_t scale = settings->mode == IMAGE_FILL_DISPLAY ?
                     (xscale > yscale ? xscale : yscale) :
                     (xscale < yscale ? xscale : yscale);
    if(scale == 0) scale = 1;
    lv_image_set_pivot(*object, original_width / 2, original_height / 2);
    lv_image_set_rotation(*object, (int32_t)settings->rotation * 10);
    lv_image_set_scale(*object, scale);
    lv_obj_set_pos(*object, (width - original_width) / 2,
                   (height - original_height) / 2);
    return 1;
}

static void clear_edit_preview(void)
{
    if(!edit_preview_path[0]) return;
    char source[sizeof(edit_preview_path) + 3];
    snprintf(source, sizeof(source), "A:%s", edit_preview_path);
    lv_image_set_src(edit_preview_image, NULL);
    lv_image_cache_drop(source);
    unlink(edit_preview_path);
    edit_preview_path[0] = '\0';
}

static int prepare_image(int index)
{
    if(index == converted_index && converted_path[0]) return 1;
    char converted[sizeof(converted_path)];
    char preview[sizeof(preview_path)];
    image_settings_t settings;
    image_settings_load(files[index].path, &settings);
    if(!image_convert_to_bmp_edit(files[index].path, files[index].name,
                                  800, 480, &settings,
                                  converted, sizeof(converted))) {
        set_status("Could not decode image. Check the file and installed format support.");
        return 0;
    }
    if(!image_convert_to_bmp_size(converted, "preview.bmp",
                                  PREVIEW_WIDTH, PREVIEW_HEIGHT,
                                  preview, sizeof(preview))) {
        unlink(converted);
        set_status("Could not prepare the image preview.");
        return 0;
    }
    clear_converted();
    snprintf(converted_path, sizeof(converted_path), "%s", converted);
    snprintf(preview_path, sizeof(preview_path), "%s", preview);
    converted_index = index;
    return 1;
}

static void select_file(int index)
{
    char message[192];
    image_settings_t settings;
    if(index < 0 || index >= file_count) return;
    if(!prepare_image(index)) return;
    image_settings_load(files[index].path, &settings);
    if(selected != index) clear_gifs();
    selected = index;
    char source[sizeof(preview_path) + 3];
    snprintf(source, sizeof(source), "A:%s", preview_path);
    lv_image_set_src(preview_image, source);
    lv_image_set_scale(preview_image, LV_SCALE_NONE);
    lv_obj_set_pos(preview_image, 0, 0);
    if(image_is_gif(files[index].path) &&
       show_gif(&preview_gif, preview_viewport, files[index].path,
                &settings, PREVIEW_WIDTH, PREVIEW_HEIGHT)) {
        lv_obj_add_flag(preview_image, LV_OBJ_FLAG_HIDDEN);
        lv_gif_resume(preview_gif);
    } else {
        lv_obj_clear_flag(preview_image, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(selected_label, files[index].name);
    lv_obj_remove_state(edit_button, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(edit_button, lv_color_hex(0x7B1FA2), 0);
    snprintf(message, sizeof(message), "%s  |  %u deg  |  %ld KB original",
             settings.mode == IMAGE_FILL_DISPLAY ? "Fill display" : "Fit whole image",
             settings.rotation, (long)(files[index].size / 1024));
    set_status(message);
}

static void row_pressed(lv_event_t *event)
{
    select_file((int)(intptr_t)lv_event_get_user_data(event));
}

static void scan_files(void)
{
    DIR *directory;
    struct dirent *entry;
    char message[96];
    int index;

    file_count = 0;
    selected = -1;
    clear_gifs();
    clear_converted();
    lv_obj_add_state(edit_button, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(edit_button, lv_color_hex(0x424B52), 0);
    lv_label_set_text(selected_label, "Choose an image from the library");
    lv_obj_clean(file_list);
    directory = opendir(PHOTO_DIR);
    if(directory == NULL) {
        lv_label_set_text(count_label, "FOLDER OFFLINE");
        set_status("Photo folder unavailable. Check the storage directory.");
        return;
    }
    while((entry = readdir(directory)) != NULL && file_count < MAX_FILES) {
        struct stat info;
        photo_file_t *file;
        if(!image_supported_name(entry->d_name)) continue;
        file = &files[file_count];
        if(strlen(entry->d_name) >= sizeof(file->name)) continue;
        if(snprintf(file->path, sizeof(file->path), "%s/%s", PHOTO_DIR, entry->d_name) >= (int)sizeof(file->path)) continue;
        snprintf(file->name, sizeof(file->name), "%s", entry->d_name);
        if(stat(file->path, &info) != 0 || !S_ISREG(info.st_mode)) continue;
        file->size = info.st_size;
        file_count++;
    }
    closedir(directory);
    qsort(files, (size_t)file_count, sizeof(files[0]), compare_files);
    for(index = 0; index < file_count; index++) {
        lv_obj_t *row = button(file_list, files[index].name, 0x26313D, 2, index * 43, 218, 38);
        lv_obj_add_event_cb(row, row_pressed, LV_EVENT_CLICKED, (void *)(intptr_t)index);
    }
    snprintf(message, sizeof(message), "%d %s", file_count, file_count == 1 ? "IMAGE" : "IMAGES");
    lv_label_set_text(count_label, message);
    if(file_count == 0) set_status("No images found. Add photos, then tap Rescan.");
    else set_status("Select an image to preview it.");
}

static void show_gallery(const char *message)
{
    if(slideshow_timer) lv_timer_pause(slideshow_timer);
    if(full_gif) lv_gif_pause(full_gif);
    if(preview_gif) lv_gif_restart(preview_gif);
    clear_edit_preview();
    lv_obj_add_flag(edit_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(viewer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(gallery, LV_OBJ_FLAG_HIDDEN);
    if(message) set_status(message);
}

static void fullscreen_tapped(lv_event_t *event)
{
    (void)event;
    show_gallery("Back in the image library.");
}

static int show_fullscreen_index(int index)
{
    if(index < 0 || index >= file_count) return 0;
    if(!prepare_image(index)) return 0;
    select_file(index);
    if(preview_gif) lv_gif_pause(preview_gif);
    char source[sizeof(converted_path) + 3];
    snprintf(source, sizeof(source), "A:%s", converted_path);
    lv_image_set_src(full_image, source);
    lv_image_set_scale(full_image, 256);
    lv_obj_set_pos(full_image, 0, 0);
    if(image_is_gif(files[index].path)) {
        image_settings_t settings;
        image_settings_load(files[index].path, &settings);
        int new_gif = full_gif == NULL;
        if(show_gif(&full_gif, viewer, files[index].path,
                    &settings, 800, 480)) {
            if(new_gif) {
                lv_obj_add_flag(full_gif, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(full_gif, fullscreen_tapped, LV_EVENT_CLICKED, NULL);
            }
            lv_obj_add_flag(full_image, LV_OBJ_FLAG_HIDDEN);
            lv_gif_restart(full_gif);
        } else lv_obj_clear_flag(full_image, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_clear_flag(full_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(gallery, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(viewer, LV_OBJ_FLAG_HIDDEN);
    return 1;
}

static void open_selected(lv_event_t *event)
{
    (void)event;
    if(selected < 0) { set_status("Select an image in the library first."); return; }
    if(show_fullscreen_index(selected)) set_status("Tap the image to return to the library.");
}

static void slide_next(lv_timer_t *timer)
{
    (void)timer;
    if(file_count == 0) return;
    if(!show_fullscreen_index((selected + 1) % file_count))
        show_gallery("Slideshow stopped: image could not be loaded.");
    else lv_timer_reset(slideshow_timer);
}

static void play_slideshow(lv_event_t *event)
{
    (void)event;
    if(file_count == 0) { set_status("No images are available for a slideshow."); return; }
    if(selected < 0) selected = 0;
    if(!show_fullscreen_index(selected)) return;
    if(slideshow_timer == NULL) slideshow_timer = lv_timer_create(slide_next, 3000, NULL);
    else lv_timer_resume(slideshow_timer);
}

static void rescan(lv_event_t *event)
{
    (void)event;
    show_gallery(NULL);
    refresh_card_mount();
    scan_files();
}

static void card_poll(lv_timer_t *timer)
{
    (void)timer;
    if(refresh_card_mount()) {
        if(!lv_obj_has_flag(viewer, LV_OBJ_FLAG_HIDDEN) ||
           !lv_obj_has_flag(edit_screen, LV_OBJ_FLAG_HIDDEN)) show_gallery(NULL);
        scan_files();
    }
}

static int is_touch_name(const char *name)
{
    char lower[256]; size_t i, length = strlen(name);
    if(length >= sizeof(lower)) length = sizeof(lower) - 1;
    for(i = 0; i < length; i++) lower[i] = name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i];
    lower[length] = '\0';
    return strstr(lower, "touch") || strstr(lower, "ft5") || strstr(lower, "goodix") || strstr(lower, "edt");
}

static void enable_touch(void)
{
    DIR *directory = opendir("/dev/input");
    struct dirent *entry;
    if(directory == NULL) return;
    while((entry = readdir(directory)) != NULL) {
        char path[128], name[256] = {0}; int fd;
        if(strncmp(entry->d_name, "event", 5) != 0) continue;
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        fd = open(path, O_RDONLY | O_NONBLOCK);
        if(fd < 0) continue;
        (void)ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        close(fd);
        if(is_touch_name(name) && lv_evdev_create(LV_INDEV_TYPE_POINTER, path)) break;
    }
    closedir(directory);
}

static void wifi_poll(lv_timer_t *timer)
{
    (void)timer;
    if(lv_obj_has_flag(wifi_screen, LV_OBJ_FLAG_HIDDEN)) return;
    char status[128];
    wifi_status(status, sizeof(status));
    lv_label_set_text(wifi_status_label, status);
}

static void wifi_hide_keyboard(lv_event_t *event)
{
    if(event && lv_event_get_code(event) == LV_EVENT_CLICKED) {
        lv_obj_t *target = lv_event_get_target(event);
        if(target == wifi_ssid || target == wifi_password ||
           target == wifi_keyboard) return;
    }
    lv_obj_add_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void wifi_choose(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    if(index >= 0 && index < WIFI_MAX_NETWORKS)
        lv_textarea_set_text(wifi_ssid, wifi_networks[index].ssid);
    wifi_hide_keyboard(NULL);
}

static void wifi_show_scan(lv_timer_t *timer)
{
    (void)timer;
    wifi_scan_timer = NULL;
    lv_obj_clean(wifi_list);
    int count = wifi_scan_results(wifi_networks, WIFI_MAX_NETWORKS);
    if(count < 0) {
        lv_label_set_text(wifi_status_label, "Scan unavailable. Check Wi-Fi service.");
        return;
    }
    for(int i = 0; i < count; i++) {
        char caption[96];
        snprintf(caption, sizeof(caption), "%s  (%d dBm)",
                 wifi_networks[i].ssid, wifi_networks[i].signal);
        lv_obj_t *row = button(wifi_list, caption, 0x26313D,
                               2, i * 36, 340, 32);
        lv_obj_add_event_cb(row, wifi_choose, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if(count == 0) lv_label_set_text(wifi_status_label, "No networks found. Tap Scan again.");
}

static void wifi_rescan(lv_event_t *event)
{
    (void)event;
    wifi_hide_keyboard(NULL);
    if(!wifi_scan_start()) {
        lv_label_set_text(wifi_status_label, "Could not scan. Check Wi-Fi service.");
        return;
    }
    lv_label_set_text(wifi_status_label, "Scanning nearby Wi-Fi networks...");
    if(wifi_scan_timer) lv_timer_delete(wifi_scan_timer);
    wifi_scan_timer = lv_timer_create(wifi_show_scan, 2200, NULL);
    lv_timer_set_repeat_count(wifi_scan_timer, 1);
}

static void wifi_connect_pressed(lv_event_t *event)
{
    (void)event;
    wifi_hide_keyboard(NULL);
    if(wifi_connect(lv_textarea_get_text(wifi_ssid),
                    lv_textarea_get_text(wifi_password))) {
        lv_label_set_text(wifi_status_label, "Connecting; saved for future boots...");
        lv_textarea_set_text(wifi_password, "");
    } else lv_label_set_text(wifi_status_label, "Could not save Wi-Fi connection. Check SSID and password.");
}

static void wifi_field_pressed(lv_event_t *event)
{
    lv_keyboard_set_textarea(wifi_keyboard, lv_event_get_target(event));
    lv_obj_clear_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(wifi_keyboard);
}

static void wifi_back_pressed(lv_event_t *event)
{
    (void)event;
    show_gallery(NULL);
}

static void wifi_open_pressed(lv_event_t *event)
{
    (void)event;
    wifi_hide_keyboard(NULL);
    if(preview_gif) lv_gif_pause(preview_gif);
    lv_obj_add_flag(gallery, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_screen, LV_OBJ_FLAG_HIDDEN);
    wifi_poll(NULL);
    wifi_rescan(NULL);
}

static void build_wifi(lv_obj_t *screen)
{
    wifi_screen = lv_obj_create(screen);
    lv_obj_set_size(wifi_screen, 800, 480);
    style_box(wifi_screen, 0x08151C, 0);
    lv_obj_add_flag(wifi_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wifi_screen, wifi_hide_keyboard, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(wifi_screen, LV_OBJ_FLAG_SCROLLABLE);
    text(wifi_screen, "WI-FI SETUP", 0xFFFFFF, 20, 14);
    lv_obj_t *back = button(wifi_screen, "BACK", 0x455A64, 680, 8, 100, 38);
    lv_obj_add_event_cb(back, wifi_back_pressed, LV_EVENT_CLICKED, NULL);
    wifi_status_label = text(wifi_screen, "Checking Wi-Fi...", 0x00BFA5, 20, 50);
    lv_obj_set_width(wifi_status_label, 760);
    lv_obj_t *scan = button(wifi_screen, "SCAN", 0x1565C0, 250, 80, 100, 38);
    lv_obj_add_event_cb(scan, wifi_rescan, LV_EVENT_CLICKED, NULL);
    text(wifi_screen, "NETWORKS", 0xFFFFFF, 20, 87);
    wifi_list = lv_obj_create(wifi_screen);
    lv_obj_set_pos(wifi_list, 18, 124);
    lv_obj_set_size(wifi_list, 356, 138);
    style_box(wifi_list, 0x18232E, 8);
    lv_obj_add_event_cb(wifi_list, wifi_hide_keyboard, LV_EVENT_CLICKED, NULL);
    text(wifi_screen, "Network name", 0xFFFFFF, 400, 85);
    wifi_ssid = lv_textarea_create(wifi_screen);
    lv_obj_set_pos(wifi_ssid, 400, 108);
    lv_obj_set_size(wifi_ssid, 370, 42);
    lv_textarea_set_one_line(wifi_ssid, true);
    lv_textarea_set_max_length(wifi_ssid, 32);
    lv_obj_add_event_cb(wifi_ssid, wifi_field_pressed, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(wifi_ssid, wifi_hide_keyboard, LV_EVENT_READY, NULL);
    text(wifi_screen, "Password", 0xFFFFFF, 400, 158);
    wifi_password = lv_textarea_create(wifi_screen);
    lv_obj_set_pos(wifi_password, 400, 180);
    lv_obj_set_size(wifi_password, 370, 42);
    lv_textarea_set_one_line(wifi_password, true);
    lv_textarea_set_password_mode(wifi_password, true);
    lv_textarea_set_max_length(wifi_password, 63);
    lv_obj_add_event_cb(wifi_password, wifi_field_pressed, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(wifi_password, wifi_hide_keyboard, LV_EVENT_READY, NULL);
    lv_obj_t *connect = button(wifi_screen, "CONNECT AND SAVE", 0x00A152, 400, 226, 370, 38);
    lv_obj_add_event_cb(connect, wifi_connect_pressed, LV_EVENT_CLICKED, NULL);
    wifi_keyboard = lv_keyboard_create(wifi_screen);
    lv_obj_set_size(wifi_keyboard, 800, 210);
    lv_obj_align(wifi_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(wifi_keyboard, wifi_password);
    lv_obj_add_event_cb(wifi_keyboard, wifi_hide_keyboard, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(wifi_keyboard, wifi_hide_keyboard, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_screen, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(wifi_poll, 3000, NULL);
}

static int edit_refresh(void)
{
    if(selected < 0 || selected >= file_count) return 0;
    char next_path[sizeof(edit_preview_path)], source[sizeof(edit_preview_path) + 3];
    if(!image_convert_to_bmp_edit(files[selected].path, files[selected].name,
                                  PREVIEW_WIDTH, PREVIEW_HEIGHT,
                                  &editing_settings, next_path,
                                  sizeof(next_path))) {
        lv_label_set_text(edit_status_label, "Could not render this edit.");
        return 0;
    }
    clear_edit_preview();
    snprintf(edit_preview_path, sizeof(edit_preview_path), "%s", next_path);
    snprintf(source, sizeof(source), "A:%s", edit_preview_path);
    lv_image_set_src(edit_preview_image, source);
    char angle[32];
    snprintf(angle, sizeof(angle), "Rotation: %u deg", editing_settings.rotation);
    lv_label_set_text(edit_rotation_label, angle);
    lv_obj_set_style_bg_color(fit_button,
        lv_color_hex(editing_settings.mode == IMAGE_FIT_WHOLE ? 0x1565C0 : 0x37474F), 0);
    lv_obj_set_style_bg_color(fill_button,
        lv_color_hex(editing_settings.mode == IMAGE_FILL_DISPLAY ? 0x1565C0 : 0x37474F), 0);
    lv_label_set_text(edit_status_label, "Preview of saved result");
    return 1;
}

static void edit_rotate(lv_event_t *event)
{
    image_settings_t previous = editing_settings;
    unsigned int delta = (intptr_t)lv_event_get_user_data(event) == 1 ? 270 : 90;
    editing_settings.rotation = (editing_settings.rotation + delta) % 360;
    if(!edit_refresh()) editing_settings = previous;
}

static void edit_mode(lv_event_t *event)
{
    image_settings_t previous = editing_settings;
    editing_settings.mode = (image_mode_t)(intptr_t)lv_event_get_user_data(event);
    if(!edit_refresh()) editing_settings = previous;
}

static void edit_cancel(lv_event_t *event)
{
    (void)event;
    show_gallery(NULL);
}

static void edit_save(lv_event_t *event)
{
    (void)event;
    if(selected < 0 || selected >= file_count) { show_gallery(NULL); return; }
    int index = selected;
    if(!image_settings_save(files[index].path, &editing_settings)) {
        lv_label_set_text(edit_status_label, "Could not save settings to photo storage.");
        return;
    }
    show_gallery(NULL);
    clear_converted();
    select_file(index);
}

static void edit_open(lv_event_t *event)
{
    (void)event;
    if(selected < 0 || selected >= file_count) return;
    image_settings_load(files[selected].path, &editing_settings);
    if(preview_gif) lv_gif_pause(preview_gif);
    lv_obj_add_flag(gallery, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(edit_screen, LV_OBJ_FLAG_HIDDEN);
    if(!edit_refresh()) show_gallery("Could not load image editor.");
}

static void build_edit(lv_obj_t *screen)
{
    edit_screen = lv_obj_create(screen);
    lv_obj_set_size(edit_screen, 800, 480);
    style_box(edit_screen, 0x08151C, 0);
    lv_obj_clear_flag(edit_screen, LV_OBJ_FLAG_SCROLLABLE);
    text(edit_screen, "EDIT IMAGE", 0xFFFFFF, 18, 18);
    edit_rotation_label = text(edit_screen, "Rotation: 0 deg", 0xFFFFFF, 18, 90);
    lv_obj_t *left = button(edit_screen, "ROTATE LEFT", 0x455A64, 18, 123, 110, 42);
    lv_obj_add_event_cb(left, edit_rotate, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_t *right = button(edit_screen, "ROTATE RIGHT", 0x455A64, 138, 123, 110, 42);
    lv_obj_add_event_cb(right, edit_rotate, LV_EVENT_CLICKED, (void *)(intptr_t)2);
    text(edit_screen, "Display mode", 0xFFFFFF, 18, 194);
    fit_button = button(edit_screen, "FIT WHOLE IMAGE", 0x1565C0, 18, 222, 230, 44);
    lv_obj_add_event_cb(fit_button, edit_mode, LV_EVENT_CLICKED, (void *)(intptr_t)IMAGE_FIT_WHOLE);
    fill_button = button(edit_screen, "FILL DISPLAY", 0x37474F, 18, 276, 230, 44);
    lv_obj_add_event_cb(fill_button, edit_mode, LV_EVENT_CLICKED, (void *)(intptr_t)IMAGE_FILL_DISPLAY);
    text(edit_screen, "FIT keeps the whole photo.", 0x00BFA5, 18, 339);
    text(edit_screen, "FILL crops the edges.", 0x00BFA5, 18, 362);

    text(edit_screen, "RESULT PREVIEW", 0xFFFFFF, 270, 56);
    lv_obj_t *frame = lv_obj_create(edit_screen);
    lv_obj_set_pos(frame, 270, 85);
    lv_obj_set_size(frame, 484, 276);
    style_box(frame, 0x000000, 8);
    lv_obj_set_style_border_color(frame, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    edit_preview_image = lv_image_create(frame);
    lv_obj_set_pos(edit_preview_image, 20, 4);
    edit_status_label = text(edit_screen, "", 0xFDD835, 270, 367);
    lv_obj_t *save = button(edit_screen, "SAVE", 0x00A152, 270, 400, 215, 44);
    lv_obj_add_event_cb(save, edit_save, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel = button(edit_screen, "CANCEL", 0x455A64, 539, 400, 215, 44);
    lv_obj_add_event_cb(cancel, edit_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(edit_screen, LV_OBJ_FLAG_HIDDEN);
}

static void build_gallery(lv_obj_t *screen)
{
    gallery = lv_obj_create(screen);
    lv_obj_set_size(gallery, 800, 480); lv_obj_clear_flag(gallery, LV_OBJ_FLAG_SCROLLABLE); style_box(gallery, 0x000000, 0);
    lv_obj_set_style_pad_all(gallery, 0, 0);
    lv_obj_t *head = lv_obj_create(gallery); lv_obj_set_size(head, 800, 68); style_box(head, 0x101010, 0);
    lv_obj_t *badge = lv_obj_create(head); lv_obj_set_size(badge, 42, 42); lv_obj_set_pos(badge, 18, 13); style_box(badge, 0xE53935, 12); storage_label = text(badge, "PI", 0xFFFFFF, 11, 13);
    text(head, "PHOTO GALLERY", 0xFFFFFF, 72, 16); text(head, "Image library", 0x00BFA5, 72, 38);
    count_label = text(head, "FOLDER OFFLINE", 0xFDD835, 450, 25);
    lv_obj_t *wifi = button(head, "WI-FI", 0x455A64, 570, 15, 100, 38); lv_obj_add_event_cb(wifi, wifi_open_pressed, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh = button(head, "RESCAN", 0x1565C0, 680, 15, 102, 38); lv_obj_add_event_cb(refresh, rescan, LV_EVENT_CLICKED, NULL);

    lv_obj_t *left = lv_obj_create(gallery); lv_obj_set_pos(left, 18, 82); lv_obj_set_size(left, 242, 358); style_box(left, 0x101010, 16);
    text(left, "IMAGE LIBRARY", 0xFF00FF, 12, 12);
    file_list = lv_obj_create(left); lv_obj_set_pos(file_list, 10, 39); lv_obj_set_size(file_list, 222, 190); style_box(file_list, 0x18232E, 8);
    lv_obj_set_style_pad_all(file_list, 0, 0); lv_obj_set_style_pad_row(file_list, 4, 0);
    text(left, "Tap a file to load it", 0x00BFA5, 12, 238);
    lv_obj_t *open = button(left, "OPEN SELECTED", 0xE53935, 10, 260, 222, 38); lv_obj_add_event_cb(open, open_selected, LV_EVENT_CLICKED, NULL);
    lv_obj_t *play = button(left, "PLAY SLIDESHOW", 0x00C853, 10, 304, 222, 38); lv_obj_add_event_cb(play, play_slideshow, LV_EVENT_CLICKED, NULL);

    lv_obj_t *right = lv_obj_create(gallery); lv_obj_set_pos(right, 274, 82); lv_obj_set_size(right, 508, 358); style_box(right, 0x101010, 16);
    text(right, "PREVIEW", 0x1565C0, 12, 12);
    edit_button = button(right, "EDIT IMAGE", 0x424B52, 382, 10, 110, 38);
    lv_obj_add_state(edit_button, LV_STATE_DISABLED);
    lv_obj_add_event_cb(edit_button, edit_open, LV_EVENT_CLICKED, NULL);
    selected_label = text(right, "Choose an image from the library", 0xFFFFFF, 12, 38);
    lv_obj_set_width(selected_label, 360);
    lv_label_set_long_mode(selected_label, LV_LABEL_LONG_DOT);
    lv_obj_t *frame = lv_obj_create(right); lv_obj_set_pos(frame, 12, 62); lv_obj_set_size(frame, 484, 276); style_box(frame, 0x000000, 12);
    lv_obj_set_style_border_color(frame, lv_color_hex(0x1565C0), 0); lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    preview_viewport = lv_obj_create(frame);
    lv_obj_set_pos(preview_viewport, 20, 4);
    lv_obj_set_size(preview_viewport, PREVIEW_WIDTH, PREVIEW_HEIGHT);
    style_box(preview_viewport, 0x000000, 0);
    lv_obj_clear_flag(preview_viewport, LV_OBJ_FLAG_SCROLLABLE);
    preview_image = lv_image_create(preview_viewport);
    lv_image_set_pivot(preview_image, 0, 0);
    lv_obj_t *footer = lv_obj_create(gallery); lv_obj_set_pos(footer, 18, 448); lv_obj_set_size(footer, 764, 24); style_box(footer, 0x003D3A, 7);
    status_label = text(footer, "Scanning photo folder...", 0xFFFFFF, 8, 4);
}

static void build_viewer(lv_obj_t *screen)
{
    viewer = lv_obj_create(screen); lv_obj_set_size(viewer, 800, 480); lv_obj_clear_flag(viewer, LV_OBJ_FLAG_SCROLLABLE); style_box(viewer, 0x000000, 0);
    lv_obj_set_style_pad_all(viewer, 0, 0);
    full_image = lv_image_create(viewer); lv_obj_add_flag(full_image, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(full_image, fullscreen_tapped, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(viewer, fullscreen_tapped, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(viewer, LV_OBJ_FLAG_HIDDEN);
}

int main(void)
{
    lv_display_t *display;
    lv_init();
    display = lv_linux_fbdev_create();
    if(display == NULL) { fprintf(stderr, "photo-viewer: cannot create framebuffer display\n"); return 1; }
    lv_linux_fbdev_set_file(display, "/dev/fb0");
    enable_touch();
    lv_obj_t *screen = lv_screen_active(); style_box(screen, 0x000000, 0);
    build_gallery(screen); build_viewer(screen); build_wifi(screen); build_edit(screen);
    refresh_card_mount();
    scan_files();
    lv_timer_create(card_poll, 5000, NULL);
    if(!media_server_start(PHOTO_DIR))
        fprintf(stderr, "photo-viewer: could not start media server\n");
    for(;;) {
        if(media_server_take_changed()) {
            if(!lv_obj_has_flag(viewer, LV_OBJ_FLAG_HIDDEN) ||
               !lv_obj_has_flag(edit_screen, LV_OBJ_FLAG_HIDDEN)) show_gallery(NULL);
            scan_files();
        }
        lv_timer_handler();
        usleep(5000);
    }
}
