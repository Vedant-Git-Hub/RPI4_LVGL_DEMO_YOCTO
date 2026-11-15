#include "video_ui.h"
#include "video_backend.h"
#include "display_layout.h"
#include "image_settings.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define VIDEO_LIMIT 64
#define FRAME_WIDTH (PHOTO_SCREEN_WIDTH - 320)
#define FRAME_HEIGHT (PHOTO_SCREEN_HEIGHT == 600 ? 396 : 270)

typedef struct { char name[128]; char path[512]; } video_item_t;

static const char *video_directory;
static void (*back_callback)(void), (*settings_back_callback)(void);
static video_item_t items[VIDEO_LIMIT];
static int item_count, selected = -1, paused;
static lv_obj_t *screen, *list, *selected_label, *status_label, *frame_image;
static lv_obj_t *edit_screen, *edit_image;
static lv_obj_t *full_screen, *full_image, *full_controls, *full_pause_caption;
static lv_obj_t *seek_slider, *seek_label;
static lv_obj_t *edit_status, *edit_rotation, *fit_button, *fill_button;
static lv_timer_t *frame_timer;
static uint8_t *pixels, *edit_pixels, *full_pixels;
static lv_image_dsc_t frame_descriptor, edit_descriptor, full_descriptor;
static video_playback_t *player;
static uint64_t last_sequence;
static double duration;
static uint32_t controls_shown_at;
static image_settings_t editing;

static void style(lv_obj_t *object, uint32_t color, int radius)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *label(lv_obj_t *parent, const char *message, uint32_t color,
                       int x, int y, int width)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_label_set_text(object, message);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(object, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(object, x, y);
    if(width) { lv_obj_set_width(object, width); lv_label_set_long_mode(object, LV_LABEL_LONG_DOT); }
    return object;
}

static lv_obj_t *button(lv_obj_t *parent, const char *caption, uint32_t color,
                        int x, int y, int width, int height, lv_obj_t **text)
{
    lv_obj_t *object = lv_button_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    style(object, color, 8);
    lv_obj_t *name = label(object, caption, 0xFFFFFF, 0, 0, width - 8);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(name);
    if(text) *text = name;
    return object;
}

static void status(const char *message)
{
    lv_label_set_text(status_label, message);
}

static int compare_items(const void *a, const void *b)
{
    return strcasecmp(((const video_item_t *)a)->name, ((const video_item_t *)b)->name);
}

static void stop_playback(void)
{
    if(player) { video_playback_stop(player); player = NULL; }
    paused = 0;
    if(full_pause_caption) lv_label_set_text(full_pause_caption, "PLAY");
    last_sequence = 0;
}

static void update_image(lv_image_dsc_t *descriptor, lv_obj_t *image)
{
    lv_image_cache_drop(descriptor);
    lv_obj_invalidate(image);
}

static void show_selected_preview(void)
{
    if(selected < 0 || selected >= item_count || !pixels) return;
    image_settings_t settings;
    image_settings_load(items[selected].path, &settings);
    if(video_render_preview(items[selected].path, FRAME_WIDTH, FRAME_HEIGHT,
                            &settings, pixels, frame_descriptor.data_size)) {
        update_image(&frame_descriptor, frame_image);
        status("Tap PLAY SELECTED to open this video.");
    } else status("Could not decode this video. Check its format and codec.");
}

static void select_item(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    if(index < 0 || index >= item_count) return;
    stop_playback();
    selected = index;
    lv_label_set_text(selected_label, items[index].name);
    show_selected_preview();
}

void video_ui_rescan(void)
{
    char previous[128] = {0};
    if(selected >= 0 && selected < item_count)
        snprintf(previous, sizeof(previous), "%s", items[selected].name);
    item_count = 0;
    selected = -1;
    lv_obj_clean(list);
    DIR *directory = opendir(video_directory);
    if(!directory) { status("Video folder unavailable."); return; }
    struct dirent *entry;
    while((entry = readdir(directory)) && item_count < VIDEO_LIMIT) {
        if(!video_supported_name(entry->d_name) || strlen(entry->d_name) >= sizeof(items[0].name)) continue;
        video_item_t *item = &items[item_count];
        if(snprintf(item->path, sizeof(item->path), "%s/%s", video_directory,
                    entry->d_name) >= (int)sizeof(item->path)) continue;
        struct stat information;
        if(stat(item->path, &information) || !S_ISREG(information.st_mode)) continue;
        snprintf(item->name, sizeof(item->name), "%s", entry->d_name);
        item_count++;
    }
    closedir(directory);
    qsort(items, (size_t)item_count, sizeof(items[0]), compare_items);
    for(int i = 0; i < item_count; i++) {
        lv_obj_t *row = button(list, items[i].name, 0x26313D,
                               2, i * 43, 214, 38, NULL);
        lv_obj_add_event_cb(row, select_item, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        if(!strcmp(previous, items[i].name)) selected = i;
    }
    if(selected >= 0) lv_label_set_text(selected_label, items[selected].name);
    else lv_label_set_text(selected_label, "Select a video from the library");
    if(!item_count) status("No videos found. Upload a video from the web page.");
    else status("Select a video, then tap PLAY SELECTED.");
}

static void rescan_pressed(lv_event_t *event)
{
    (void)event;
    stop_playback();
    video_ui_rescan();
}

static void back_pressed(lv_event_t *event)
{
    (void)event;
    video_ui_close();
    if(back_callback) back_callback();
}

static void show_controls(void)
{
    lv_obj_clear_flag(full_controls, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(full_controls);
    controls_shown_at = lv_tick_get();
}

static void full_tapped(lv_event_t *event)
{
    (void)event;
    show_controls();
}

static void full_back_pressed(lv_event_t *event)
{
    (void)event;
    stop_playback();
    lv_obj_add_flag(full_screen, LV_OBJ_FLAG_HIDDEN);
    if(selected >= 0) show_selected_preview();
}

static void update_seek_label(double position)
{
    char caption[40];
    int now = (int)position, total = (int)duration;
    snprintf(caption, sizeof(caption), "%02d:%02d / %02d:%02d",
             now / 60, now % 60, total / 60, total % 60);
    lv_label_set_text(seek_label, caption);
}

static int play_at(double start_seconds)
{
    if(selected < 0 || selected >= item_count || !full_pixels) return 0;
    stop_playback();
    image_settings_t settings;
    image_settings_load(items[selected].path, &settings);
    duration = video_probe_duration(items[selected].path);
    lv_slider_set_range(seek_slider, 0, duration > 0 ? (int)(duration + 0.5) : 100);
    if(duration > 0 && start_seconds >= duration) start_seconds = 0;
    char error[128] = {0};
    player = video_playback_start(items[selected].path, PHOTO_SCREEN_WIDTH,
                                  PHOTO_SCREEN_HEIGHT, &settings, start_seconds,
                                  error, sizeof(error));
    if(!player) {
        status(error[0] ? error : "Could not start video playback.");
        return 0;
    }
    lv_label_set_text(full_pause_caption, "PAUSE");
    lv_slider_set_value(seek_slider, (int)start_seconds, LV_ANIM_OFF);
    update_seek_label(start_seconds);
    lv_obj_clear_flag(full_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(full_screen);
    show_controls();
    return 1;
}

static void play_pressed(lv_event_t *event)
{
    (void)event;
    if(selected < 0 || selected >= item_count) { status("Select a video first."); return; }
    if(play_at(0)) status("Video playing full screen. Tap to show controls.");
}

static void pause_pressed(lv_event_t *event)
{
    (void)event;
    if(!player) return;
    paused = !paused;
    video_playback_pause(player, paused);
    lv_label_set_text(full_pause_caption, paused ? "PLAY" : "PAUSE");
    status(paused ? "Video paused." : "Video playing.");
    show_controls();
}

static void full_pause_pressed(lv_event_t *event)
{
    if(player) pause_pressed(event);
    else play_at(0);
}

static void seek_released(lv_event_t *event)
{
    (void)event;
    if(duration <= 0) return;
    double position = lv_slider_get_value(seek_slider);
    int was_paused = paused;
    if(play_at(position) && was_paused) {
        video_playback_pause(player, 1);
        paused = 1;
        lv_label_set_text(full_pause_caption, "PLAY");
    }
    show_controls();
}

static void seek_touching(lv_event_t *event)
{
    (void)event;
    controls_shown_at = lv_tick_get();
    if(duration > 0) update_seek_label(lv_slider_get_value(seek_slider));
}

static void frame_tick(lv_timer_t *timer)
{
    (void)timer;
    if(!lv_obj_has_flag(full_screen, LV_OBJ_FLAG_HIDDEN) &&
       !lv_obj_has_flag(full_controls, LV_OBJ_FLAG_HIDDEN) && !paused &&
       !lv_obj_has_state(seek_slider, LV_STATE_PRESSED) &&
       lv_tick_get() - controls_shown_at > 3000)
        lv_obj_add_flag(full_controls, LV_OBJ_FLAG_HIDDEN);
    if(!player || lv_obj_has_flag(full_screen, LV_OBJ_FLAG_HIDDEN)) return;
    if(video_playback_copy_frame(player, full_pixels, full_descriptor.data_size,
                                 &last_sequence)) update_image(&full_descriptor, full_image);
    double position = video_playback_position(player);
    if(duration > 0) {
        if(position > duration) position = duration;
        if(!lv_obj_has_state(seek_slider, LV_STATE_PRESSED)) {
            lv_slider_set_value(seek_slider, (int)position, LV_ANIM_OFF);
            update_seek_label(position);
        }
    }
    if(video_playback_finished(player)) {
        stop_playback();
        if(duration > 0) lv_slider_set_value(seek_slider, (int)duration, LV_ANIM_OFF);
        show_controls();
        status("Playback finished. Tap PLAY to replay, or BACK to browse.");
    }
}

static int edit_refresh(void)
{
    if(selected < 0 || selected >= item_count || !edit_pixels) return 0;
    if(!video_render_preview(items[selected].path, FRAME_WIDTH, FRAME_HEIGHT,
                             &editing, edit_pixels, edit_descriptor.data_size)) {
        lv_label_set_text(edit_status, "Could not render this video preview.");
        return 0;
    }
    update_image(&edit_descriptor, edit_image);
    char angle[32];
    snprintf(angle, sizeof(angle), "Rotation: %u deg", editing.rotation);
    lv_label_set_text(edit_rotation, angle);
    lv_obj_set_style_bg_color(fit_button,
        lv_color_hex(editing.mode == IMAGE_FIT_WHOLE ? 0x1565C0 : 0x37474F), 0);
    lv_obj_set_style_bg_color(fill_button,
        lv_color_hex(editing.mode == IMAGE_FILL_DISPLAY ? 0x1565C0 : 0x37474F), 0);
    lv_label_set_text(edit_status, "Preview of saved playback settings");
    return 1;
}

static void edit_rotate(lv_event_t *event)
{
    image_settings_t old = editing;
    editing.rotation = (editing.rotation +
                       ((intptr_t)lv_event_get_user_data(event) == 1 ? 270 : 90)) % 360;
    if(!edit_refresh()) editing = old;
}

static void edit_mode(lv_event_t *event)
{
    image_settings_t old = editing;
    editing.mode = (image_mode_t)(intptr_t)lv_event_get_user_data(event);
    if(!edit_refresh()) editing = old;
}

static void edit_finish(int save)
{
    if(save && (selected < 0 || selected >= item_count ||
                !image_settings_save(items[selected].path, &editing))) {
        lv_label_set_text(edit_status, "Could not save video settings.");
        return;
    }
    lv_obj_add_flag(edit_screen, LV_OBJ_FLAG_HIDDEN);
    if(settings_back_callback) settings_back_callback();
}

static void edit_save(lv_event_t *event) { (void)event; edit_finish(1); }
static void edit_cancel(lv_event_t *event) { (void)event; edit_finish(0); }

void video_ui_open_settings(void)
{
    if(selected < 0 || selected >= item_count) return;
    image_settings_load(items[selected].path, &editing);
    lv_obj_clear_flag(edit_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(edit_screen);
    if(!edit_refresh()) edit_finish(0);
}

int video_ui_has_selection(void) { return selected >= 0 && selected < item_count; }
int video_ui_is_open(void) { return screen && !lv_obj_has_flag(screen, LV_OBJ_FLAG_HIDDEN); }

void video_ui_open(void)
{
    video_ui_rescan();
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(screen);
    if(selected >= 0) show_selected_preview();
}

void video_ui_close(void)
{
    stop_playback();
    lv_obj_add_flag(full_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(edit_screen, LV_OBJ_FLAG_HIDDEN);
}

void video_ui_build(lv_obj_t *root, const char *directory,
                    void (*on_back)(void), void (*on_settings_back)(void))
{
    video_directory = directory;
    back_callback = on_back;
    settings_back_callback = on_settings_back;
    size_t frame_size = (size_t)FRAME_WIDTH * FRAME_HEIGHT * 3;
    pixels = calloc(1, frame_size);
    edit_pixels = calloc(1, frame_size);
    size_t full_size = (size_t)PHOTO_SCREEN_WIDTH * PHOTO_SCREEN_HEIGHT * 3;
    full_pixels = calloc(1, full_size);
    frame_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    edit_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    full_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    frame_descriptor.header.cf = LV_COLOR_FORMAT_RGB888;
    frame_descriptor.header.w = edit_descriptor.header.w = FRAME_WIDTH;
    frame_descriptor.header.h = edit_descriptor.header.h = FRAME_HEIGHT;
    frame_descriptor.header.stride = edit_descriptor.header.stride = FRAME_WIDTH * 3;
    frame_descriptor.data_size = edit_descriptor.data_size = frame_size;
    frame_descriptor.data = pixels;
    edit_descriptor.header.cf = LV_COLOR_FORMAT_RGB888;
    edit_descriptor.data = edit_pixels;
    full_descriptor.header.cf = LV_COLOR_FORMAT_RGB888;
    full_descriptor.header.w = PHOTO_SCREEN_WIDTH;
    full_descriptor.header.h = PHOTO_SCREEN_HEIGHT;
    full_descriptor.header.stride = PHOTO_SCREEN_WIDTH * 3;
    full_descriptor.data_size = full_size;
    full_descriptor.data = full_pixels;

    screen = lv_obj_create(root);
    lv_obj_set_size(screen, PHOTO_SCREEN_WIDTH, PHOTO_SCREEN_HEIGHT);
    style(screen, 0x08151C, 0);
    label(screen, "VIDEO PLAYER", 0xFFFFFF, 20, 16, 260);
    lv_obj_t *home = button(screen, "HOME", 0x455A64,
                            PHOTO_SCREEN_WIDTH - 228, 12, 100, 42, NULL);
    lv_obj_add_event_cb(home, back_pressed, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rescan = button(screen, "RESCAN", 0x1565C0,
                              PHOTO_SCREEN_WIDTH - 116, 12, 100, 42, NULL);
    lv_obj_add_event_cb(rescan, rescan_pressed, LV_EVENT_CLICKED, NULL);

    lv_obj_t *library = lv_obj_create(screen);
    lv_obj_set_pos(library, 18, 82);
    lv_obj_set_size(library, 242, PHOTO_SCREEN_HEIGHT - 100);
    style(library, 0x101010, 12);
    label(library, "VIDEO LIBRARY", 0xFF00FF, 12, 12, 215);
    list = lv_obj_create(library);
    lv_obj_set_pos(list, 10, 39);
    lv_obj_set_size(list, 222, PHOTO_SCREEN_HEIGHT - 220);
    style(list, 0x18232E, 8);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_t *play = button(library, "PLAY SELECTED", 0x00A152,
                            10, PHOTO_SCREEN_HEIGHT - 158, 222, 44, NULL);
    lv_obj_add_event_cb(play, play_pressed, LV_EVENT_CLICKED, NULL);

    selected_label = label(screen, "Select a video from the library", 0xFFFFFF,
                           288, 70, FRAME_WIDTH);
    lv_obj_t *window = lv_obj_create(screen);
    lv_obj_set_pos(window, 288, 100);
    lv_obj_set_size(window, FRAME_WIDTH, FRAME_HEIGHT);
    style(window, 0x000000, 8);
    if(pixels) {
        frame_image = lv_image_create(window);
        lv_image_set_src(frame_image, &frame_descriptor);
    }
    status_label = label(screen, "Select a video, then tap PLAY SELECTED.",
                         0xFDD835, 288, PHOTO_SCREEN_HEIGHT - 38, FRAME_WIDTH);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);

    full_screen = lv_obj_create(root);
    lv_obj_set_size(full_screen, PHOTO_SCREEN_WIDTH, PHOTO_SCREEN_HEIGHT);
    style(full_screen, 0x000000, 0);
    lv_obj_add_flag(full_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(full_screen, full_tapped, LV_EVENT_CLICKED, NULL);
    if(full_pixels) {
        full_image = lv_image_create(full_screen);
        lv_image_set_src(full_image, &full_descriptor);
        lv_obj_add_flag(full_image, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(full_image, full_tapped, LV_EVENT_CLICKED, NULL);
    }
    full_controls = lv_obj_create(full_screen);
    lv_obj_set_size(full_controls, PHOTO_SCREEN_WIDTH, PHOTO_SCREEN_HEIGHT);
    style(full_controls, 0x000000, 0);
    lv_obj_set_style_bg_opa(full_controls, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(full_controls, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *top = lv_obj_create(full_controls);
    lv_obj_set_size(top, PHOTO_SCREEN_WIDTH, 66);
    style(top, 0x07121C, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_70, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *full_back = button(top, "BACK", 0x455A64,
                                 PHOTO_SCREEN_WIDTH - 112, 10, 100, 44, NULL);
    lv_obj_add_event_cb(full_back, full_back_pressed, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bottom = lv_obj_create(full_controls);
    lv_obj_set_pos(bottom, 0, PHOTO_SCREEN_HEIGHT - 100);
    lv_obj_set_size(bottom, PHOTO_SCREEN_WIDTH, 100);
    style(bottom, 0x07121C, 0);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_70, 0);
    lv_obj_clear_flag(bottom, LV_OBJ_FLAG_CLICKABLE);
    seek_slider = lv_slider_create(bottom);
    lv_obj_set_pos(seek_slider, 20, 17);
    lv_obj_set_size(seek_slider, PHOTO_SCREEN_WIDTH - 210, 14);
    lv_slider_set_range(seek_slider, 0, 100);
    lv_obj_add_event_cb(seek_slider, seek_released, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(seek_slider, seek_touching, LV_EVENT_PRESSING, NULL);
    seek_label = label(bottom, "00:00 / 00:00", 0xFFFFFF,
                       PHOTO_SCREEN_WIDTH - 175, 15, 165);
    lv_obj_t *full_pause = button(bottom, "PAUSE", 0x1565C0,
                                  20, 48, 130, 42, &full_pause_caption);
    lv_obj_add_event_cb(full_pause, full_pause_pressed, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(full_screen, LV_OBJ_FLAG_HIDDEN);
    frame_timer = lv_timer_create(frame_tick, 40, NULL);

    edit_screen = lv_obj_create(root);
    lv_obj_set_size(edit_screen, PHOTO_SCREEN_WIDTH, PHOTO_SCREEN_HEIGHT);
    style(edit_screen, 0x08151C, 0);
    label(edit_screen, "EDIT VIDEO", 0xFFFFFF, 18, 18, 250);
    edit_rotation = label(edit_screen, "Rotation: 0 deg", 0xFFFFFF, 18, 82, 245);
    lv_obj_t *left = button(edit_screen, "ROTATE LEFT", 0x455A64, 18, 115, 112, 42, NULL);
    lv_obj_add_event_cb(left, edit_rotate, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_t *right = button(edit_screen, "ROTATE RIGHT", 0x455A64, 138, 115, 112, 42, NULL);
    lv_obj_add_event_cb(right, edit_rotate, LV_EVENT_CLICKED, (void *)(intptr_t)2);
    label(edit_screen, "Display mode", 0xFFFFFF, 18, 184, 220);
    fit_button = button(edit_screen, "FIT WHOLE VIDEO", 0x1565C0, 18, 214, 230, 44, NULL);
    lv_obj_add_event_cb(fit_button, edit_mode, LV_EVENT_CLICKED, (void *)(intptr_t)IMAGE_FIT_WHOLE);
    fill_button = button(edit_screen, "FILL DISPLAY", 0x37474F, 18, 268, 230, 44, NULL);
    lv_obj_add_event_cb(fill_button, edit_mode, LV_EVENT_CLICKED, (void *)(intptr_t)IMAGE_FILL_DISPLAY);
    label(edit_screen, "Original video is unchanged.", 0x00BFA5, 18, 337, 245);
    label(edit_screen, "RESULT PREVIEW", 0xFFFFFF, 288, 56, FRAME_WIDTH);
    lv_obj_t *edit_window = lv_obj_create(edit_screen);
    lv_obj_set_pos(edit_window, 288, 85);
    lv_obj_set_size(edit_window, FRAME_WIDTH, FRAME_HEIGHT);
    style(edit_window, 0x000000, 8);
    if(edit_pixels) {
        edit_image = lv_image_create(edit_window);
        lv_image_set_src(edit_image, &edit_descriptor);
    }
    edit_status = label(edit_screen, "", 0xFDD835, 288,
                        PHOTO_SCREEN_HEIGHT - 91, FRAME_WIDTH);
    lv_obj_t *save = button(edit_screen, "SAVE", 0x00A152,
                            288, PHOTO_SCREEN_HEIGHT - 65, 200, 44, NULL);
    lv_obj_add_event_cb(save, edit_save, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel = button(edit_screen, "CANCEL", 0x455A64,
                              500, PHOTO_SCREEN_HEIGHT - 65, 200, 44, NULL);
    lv_obj_add_event_cb(cancel, edit_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(edit_screen, LV_OBJ_FLAG_HIDDEN);
}
