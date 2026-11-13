#include "camera_ui.h"
#include "camera_backend.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CAMERA_PREVIEW_WIDTH 560
#define CAMERA_PREVIEW_HEIGHT 320
#define CAMERA_CONFIG "/opt/photo-viewer/camera.conf"

static const char *photo_dir;
static void (*back_callback)(void);
static void (*saved_callback)(const char *name);
static void (*settings_back_callback)(void);
static lv_obj_t *camera_screen, *camera_image, *camera_status, *camera_title;
static lv_obj_t *capture_button, *settings_layer, *settings_dropdown;
static lv_obj_t *settings_status;
static lv_obj_t *save_layer, *filename_field, *save_keyboard, *save_status;
static lv_timer_t *camera_timer;
static camera_device_t *camera;
static camera_mode_t modes[CAMERA_MAX_MODES];
static int mode_count, selected_mode, has_frame, retry_ticks;
static char device_path[64], camera_name[64];
static uint8_t *preview_pixels, *pending_jpeg;
static size_t pending_size;
static lv_image_dsc_t preview_descriptor;

static void style(lv_obj_t *object, uint32_t color)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *label(lv_obj_t *parent, const char *caption, uint32_t color,
                       int x, int y, int width)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_label_set_text(object, caption);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(object, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(object, x, y);
    if(width > 0) {
        lv_obj_set_width(object, width);
        lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
    }
    return object;
}

static lv_obj_t *button(lv_obj_t *parent, const char *caption, uint32_t color,
                        int x, int y, int width, int height)
{
    lv_obj_t *object = lv_button_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    style(object, color);
    lv_obj_set_style_radius(object, 8, 0);
    lv_obj_t *text = label(object, caption, 0xFFFFFF, 0, 0, width - 8);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(text);
    return object;
}

static void status(const char *message)
{
    lv_label_set_text(camera_status, message);
}

static void set_capture_enabled(int enabled)
{
    if(enabled) lv_obj_remove_state(capture_button, LV_STATE_DISABLED);
    else lv_obj_add_state(capture_button, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(capture_button,
                              lv_color_hex(enabled ? 0x00A152 : 0x424B52), 0);
}

static void clear_pending(void)
{
    free(pending_jpeg);
    pending_jpeg = NULL;
    pending_size = 0;
}

static int saved_resolution(uint32_t *width, uint32_t *height, uint32_t *format)
{
    FILE *file = fopen(CAMERA_CONFIG, "r");
    if(!file) return 0;
    *format = 0;
    int valid = fscanf(file, "%u %u %u", width, height, format) >= 2;
    fclose(file);
    return valid;
}

static void remember_resolution(const camera_mode_t *mode)
{
    char temporary[] = CAMERA_CONFIG ".XXXXXX";
    int descriptor = mkstemp(temporary);
    if(descriptor < 0) return;
    FILE *file = fdopen(descriptor, "w");
    if(!file) { close(descriptor); unlink(temporary); return; }
    int okay = fprintf(file, "%u %u %u\n", mode->width, mode->height,
                       mode->format) > 0 &&
               fflush(file) == 0 && fsync(descriptor) == 0;
    if(fclose(file)) okay = 0;
    if(okay) okay = rename(temporary, CAMERA_CONFIG) == 0;
    if(!okay) unlink(temporary);
}

static void start_mode(int index)
{
    if(camera) { camera_stop(camera); camera = NULL; }
    has_frame = 0;
    set_capture_enabled(0);
    if(preview_pixels) {
        memset(preview_pixels, 0, (size_t)CAMERA_PREVIEW_WIDTH * CAMERA_PREVIEW_HEIGHT * 3);
        lv_image_cache_drop(&preview_descriptor);
        lv_obj_invalidate(camera_image);
    }
    if(index < 0 || index >= mode_count) return;
    selected_mode = index;
    char error[128] = {0};
    camera = camera_start(device_path, &modes[index], error, sizeof(error));
    if(!camera) {
        status(error[0] ? error : "Could not start the camera.");
        return;
    }
    char title[160];
    snprintf(title, sizeof(title), "%s  |  %u x %u  |  %s", camera_name,
             modes[index].width, modes[index].height,
             camera_format_name(modes[index].format));
    lv_label_set_text(camera_title, title);
    status("Waiting for the first camera frame...");
}

static void probe_camera(void)
{
    mode_count = camera_discover(device_path, sizeof(device_path), camera_name,
                                 sizeof(camera_name), modes, CAMERA_MAX_MODES);
    if(mode_count <= 0) {
        lv_label_set_text(camera_title, "USB CAMERA");
        status("No supported USB camera found. Connect a UVC camera; retrying.");
        set_capture_enabled(0);
        return;
    }
    char options[CAMERA_MAX_MODES * 32] = {0};
    size_t used = 0;
    for(int i = 0; i < mode_count; i++) {
        int added = snprintf(options + used, sizeof(options) - used,
                             "%s%u x %u  (%s)", i ? "\n" : "",
                             modes[i].width, modes[i].height,
                             camera_format_name(modes[i].format));
        if(added < 0 || (size_t)added >= sizeof(options) - used) break;
        used += (size_t)added;
    }
    lv_dropdown_set_options(settings_dropdown, options);
    uint32_t desired_width = 0, desired_height = 0, desired_format = 0;
    int remembered = saved_resolution(&desired_width, &desired_height,
                                       &desired_format);
    int chosen = 0;
    for(int i = 0; i < mode_count; i++) {
        if(remembered && modes[i].width == desired_width &&
           modes[i].height == desired_height &&
           (!desired_format || modes[i].format == desired_format)) {
            chosen = i; break;
        }
        if(!remembered && modes[i].width == 640 && modes[i].height == 480) {
            chosen = i; break;
        }
    }
    lv_dropdown_set_selected(settings_dropdown, chosen);
    start_mode(chosen);
}

static void camera_tick(lv_timer_t *timer)
{
    (void)timer;
    if(lv_obj_has_flag(camera_screen, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(save_layer, LV_OBJ_FLAG_HIDDEN) ||
       !lv_obj_has_flag(settings_layer, LV_OBJ_FLAG_HIDDEN)) return;
    if(!camera) {
        if(++retry_ticks >= 20) { retry_ticks = 0; probe_camera(); }
        return;
    }
    int result = camera_poll(camera, preview_pixels, CAMERA_PREVIEW_WIDTH,
                             CAMERA_PREVIEW_HEIGHT);
    if(result < 0) {
        camera_stop(camera);
        camera = NULL;
        has_frame = 0;
        set_capture_enabled(0);
        status("Camera disconnected or stopped. Retrying...");
        retry_ticks = 0;
        return;
    }
    if(result > 0) {
        lv_image_cache_drop(&preview_descriptor);
        lv_obj_invalidate(camera_image);
        if(!has_frame) {
            has_frame = 1;
            set_capture_enabled(1);
            status("Live camera preview. Tap Capture to take a photo.");
        }
    }
}

static void back_pressed(lv_event_t *event)
{
    (void)event;
    if(back_callback) back_callback();
}

static void capture_pressed(lv_event_t *event)
{
    (void)event;
    if(!camera || !has_frame) return;
    clear_pending();
    if(!camera_capture_jpeg(camera, &pending_jpeg, &pending_size)) {
        status("Could not capture this frame. Try again.");
        return;
    }
    time_t now = time(NULL);
    struct tm local;
    char filename[64];
    if(localtime_r(&now, &local))
        strftime(filename, sizeof(filename), "Photo_%Y%m%d_%H%M%S", &local);
    else snprintf(filename, sizeof(filename), "Photo_%ld", (long)now);
    lv_textarea_set_text(filename_field, filename);
    lv_label_set_text(save_status, "Enter a name, then save the captured photo.");
    lv_obj_add_flag(save_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(save_layer, LV_OBJ_FLAG_HIDDEN);
}

static int valid_name(const char *name)
{
    size_t length = strlen(name);
    if(length < 1 || length > 80) return 0;
    for(size_t i = 0; i < length; i++) {
        char c = name[i];
        if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' '))
            return 0;
    }
    return 1;
}

static void save_pressed(lv_event_t *event)
{
    (void)event;
    if(!pending_jpeg || !pending_size) return;
    const char *name = lv_textarea_get_text(filename_field);
    if(!valid_name(name)) {
        lv_label_set_text(save_status, "Use letters, numbers, spaces, _ or - (max 80).");
        return;
    }
    char filename[128], path[512];
    snprintf(filename, sizeof(filename), "%s.jpg", name);
    if(snprintf(path, sizeof(path), "%s/%s", photo_dir, filename) >= (int)sizeof(path)) {
        lv_label_set_text(save_status, "The filename is too long.");
        return;
    }
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if(descriptor < 0) {
        lv_label_set_text(save_status, errno == EEXIST ?
                          "That name exists. Choose another." :
                          "Could not create photo on current storage.");
        return;
    }
    size_t written = 0;
    int okay = 1;
    while(written < pending_size) {
        ssize_t count = write(descriptor, pending_jpeg + written,
                              pending_size - written);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) { okay = 0; break; }
        written += (size_t)count;
    }
    if(okay) okay = fsync(descriptor) == 0;
    if(close(descriptor)) okay = 0;
    if(!okay) {
        unlink(path);
        lv_label_set_text(save_status, "Could not finish saving the photo.");
        return;
    }
    clear_pending();
    lv_obj_add_flag(save_layer, LV_OBJ_FLAG_HIDDEN);
    if(saved_callback) saved_callback(filename);
}

static void save_cancel(lv_event_t *event)
{
    (void)event;
    clear_pending();
    lv_obj_add_flag(save_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(save_layer, LV_OBJ_FLAG_HIDDEN);
}

static void name_pressed(lv_event_t *event)
{
    (void)event;
    lv_keyboard_set_textarea(save_keyboard, filename_field);
    lv_obj_clear_flag(save_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(save_keyboard);
}

static void keyboard_done(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(save_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void keyboard_outside(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_target(event);
    if(target != filename_field && target != save_keyboard)
        keyboard_done(NULL);
}

static void settings_pressed(lv_event_t *event)
{
    (void)event;
    mode_count = camera_discover(device_path, sizeof(device_path), camera_name,
                                 sizeof(camera_name), modes, CAMERA_MAX_MODES);
    if(!mode_count) {
        lv_dropdown_set_options(settings_dropdown, "No supported camera connected");
        lv_label_set_text(settings_status, "Connect a USB UVC camera to list its modes.");
    } else {
        char options[CAMERA_MAX_MODES * 32] = {0};
        size_t used = 0;
        for(int i = 0; i < mode_count; i++) {
            int added = snprintf(options + used, sizeof(options) - used,
                                 "%s%u x %u  (%s)", i ? "\n" : "",
                                 modes[i].width, modes[i].height,
                                 camera_format_name(modes[i].format));
            if(added < 0 || (size_t)added >= sizeof(options) - used) break;
            used += (size_t)added;
        }
        lv_dropdown_set_options(settings_dropdown, options);
        uint32_t width = 0, height = 0, format = 0;
        int remembered = saved_resolution(&width, &height, &format);
        int chosen = 0;
        for(int i = 0; i < mode_count; i++) {
            if(remembered && modes[i].width == width && modes[i].height == height &&
               (!format || modes[i].format == format)) { chosen = i; break; }
        }
        lv_dropdown_set_selected(settings_dropdown, chosen);
        lv_label_set_text(settings_status, "Select the resolution and camera format.");
    }
    lv_obj_clear_flag(settings_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(settings_layer);
}

static void settings_cancel(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(settings_layer, LV_OBJ_FLAG_HIDDEN);
    if(settings_back_callback) settings_back_callback();
}

static void settings_apply(lv_event_t *event)
{
    (void)event;
    int index = (int)lv_dropdown_get_selected(settings_dropdown);
    if(index < 0 || index >= mode_count) {
        lv_label_set_text(settings_status, "No camera resolution is available.");
        return;
    }
    remember_resolution(&modes[index]);
    lv_obj_add_flag(settings_layer, LV_OBJ_FLAG_HIDDEN);
    if(settings_back_callback) settings_back_callback();
}

void camera_ui_build(lv_obj_t *root, const char *photo_directory,
                     void (*on_back)(void), void (*on_saved)(const char *name),
                     void (*on_settings_back)(void))
{
    photo_dir = photo_directory;
    back_callback = on_back;
    saved_callback = on_saved;
    settings_back_callback = on_settings_back;
    camera_screen = lv_obj_create(root);
    lv_obj_set_size(camera_screen, 800, 480);
    style(camera_screen, 0x08151C);
    camera_title = label(camera_screen, "USB CAMERA", 0xFFFFFF, 20, 22, 620);
    lv_obj_t *back = button(camera_screen, "BACK", 0x455A64, 680, 12, 100, 42);
    lv_obj_add_event_cb(back, back_pressed, LV_EVENT_CLICKED, NULL);
    lv_obj_t *window = lv_obj_create(camera_screen);
    lv_obj_set_pos(window, 18, 70);
    lv_obj_set_size(window, CAMERA_PREVIEW_WIDTH + 4, CAMERA_PREVIEW_HEIGHT + 4);
    style(window, 0x000000);
    lv_obj_set_style_border_color(window, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_border_width(window, 2, 0);
    preview_pixels = calloc((size_t)CAMERA_PREVIEW_WIDTH * CAMERA_PREVIEW_HEIGHT, 3);
    preview_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    preview_descriptor.header.cf = LV_COLOR_FORMAT_RGB888;
    preview_descriptor.header.w = CAMERA_PREVIEW_WIDTH;
    preview_descriptor.header.h = CAMERA_PREVIEW_HEIGHT;
    preview_descriptor.header.stride = CAMERA_PREVIEW_WIDTH * 3;
    preview_descriptor.data_size = CAMERA_PREVIEW_WIDTH * CAMERA_PREVIEW_HEIGHT * 3;
    preview_descriptor.data = preview_pixels;
    camera_image = lv_image_create(window);
    lv_obj_set_pos(camera_image, 0, 0);
    if(preview_pixels) lv_image_set_src(camera_image, &preview_descriptor);
    capture_button = button(camera_screen, "CAPTURE", 0x424B52, 604, 160, 170, 56);
    lv_obj_add_event_cb(capture_button, capture_pressed, LV_EVENT_CLICKED, NULL);
    set_capture_enabled(0);
    camera_status = label(camera_screen, "Connect a USB camera.", 0x00BFA5,
                          20, 418, 760);

    settings_layer = lv_obj_create(root);
    lv_obj_set_size(settings_layer, 800, 480);
    style(settings_layer, 0x08151C);
    label(settings_layer, "CAMERA SETTINGS", 0xFFFFFF, 30, 30, 700);
    label(settings_layer, "Resolution reported by the camera", 0x00BFA5,
          170, 100, 480);
    settings_dropdown = lv_dropdown_create(settings_layer);
    lv_obj_set_pos(settings_dropdown, 170, 140);
    lv_obj_set_size(settings_dropdown, 460, 48);
    lv_dropdown_set_options(settings_dropdown, "No camera modes");
    settings_status = label(settings_layer, "", 0xFDD835, 170, 205, 500);
    lv_obj_t *apply = button(settings_layer, "APPLY", 0x00A152, 170, 245, 205, 52);
    lv_obj_add_event_cb(apply, settings_apply, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_settings = button(settings_layer, "CANCEL", 0x455A64,
                                       425, 245, 205, 52);
    lv_obj_add_event_cb(cancel_settings, settings_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(settings_layer, LV_OBJ_FLAG_HIDDEN);

    save_layer = lv_obj_create(root);
    lv_obj_set_size(save_layer, 800, 480);
    style(save_layer, 0x08151C);
    lv_obj_add_flag(save_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(save_layer, keyboard_outside, LV_EVENT_CLICKED, NULL);
    label(save_layer, "SAVE CAPTURE", 0xFFFFFF, 24, 22, 700);
    label(save_layer, "Photo name (without .jpg)", 0x00BFA5, 100, 73, 600);
    filename_field = lv_textarea_create(save_layer);
    lv_obj_set_pos(filename_field, 100, 104);
    lv_obj_set_size(filename_field, 600, 46);
    lv_textarea_set_one_line(filename_field, true);
    lv_textarea_set_max_length(filename_field, 80);
    lv_obj_add_event_cb(filename_field, name_pressed, LV_EVENT_CLICKED, NULL);
    save_status = label(save_layer, "", 0xFDD835, 100, 158, 600);
    lv_obj_t *save = button(save_layer, "SAVE", 0x00A152, 100, 194, 260, 52);
    lv_obj_add_event_cb(save, save_pressed, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_save = button(save_layer, "CANCEL", 0x455A64, 440, 194, 260, 52);
    lv_obj_add_event_cb(cancel_save, save_cancel, LV_EVENT_CLICKED, NULL);
    save_keyboard = lv_keyboard_create(save_layer);
    lv_obj_set_size(save_keyboard, 800, 210);
    lv_obj_align(save_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(save_keyboard, filename_field);
    lv_obj_add_event_cb(save_keyboard, keyboard_done, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(save_keyboard, keyboard_done, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(save_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(save_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(camera_screen, LV_OBJ_FLAG_HIDDEN);
    camera_timer = lv_timer_create(camera_tick, 100, NULL);
    lv_timer_pause(camera_timer);
}

void camera_ui_open(void)
{
    if(!camera_screen) return;
    lv_obj_clear_flag(camera_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(camera_screen);
    lv_obj_move_foreground(settings_layer);
    lv_obj_move_foreground(save_layer);
    retry_ticks = 0;
    if(!preview_pixels) {
        status("Camera preview memory unavailable.");
        return;
    }
    probe_camera();
    lv_timer_resume(camera_timer);
}

void camera_ui_open_settings(void)
{
    if(!settings_layer) return;
    settings_pressed(NULL);
}

void camera_ui_close(void)
{
    if(!camera_screen) return;
    lv_timer_pause(camera_timer);
    if(camera) { camera_stop(camera); camera = NULL; }
    clear_pending();
    has_frame = 0;
    mode_count = 0;
    set_capture_enabled(0);
    lv_obj_add_flag(settings_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(save_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(save_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(camera_screen, LV_OBJ_FLAG_HIDDEN);
}

int camera_ui_is_open(void)
{
    return camera_screen && !lv_obj_has_flag(camera_screen, LV_OBJ_FLAG_HIDDEN);
}
