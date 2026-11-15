#ifndef VIDEO_BACKEND_H
#define VIDEO_BACKEND_H

#include "image_settings.h"

#include <stddef.h>
#include <stdint.h>

typedef struct video_playback video_playback_t;

int video_supported_name(const char *name);
int video_probe_file(const char *path);
double video_probe_duration(const char *path);
double video_probe_frame_rate(const char *path);
int video_render_preview(const char *path, unsigned int width, unsigned int height,
                         const image_settings_t *settings, uint8_t *pixels,
                         size_t capacity);
video_playback_t *video_playback_start(const char *path, unsigned int width,
                                      unsigned int height,
                                      const image_settings_t *settings,
                                      double start_seconds,
                                      char *error, size_t error_size);
double video_playback_position(video_playback_t *player);
int video_playback_copy_frame(video_playback_t *player, uint8_t *destination,
                              size_t capacity, uint64_t *last_sequence);
int video_playback_finished(video_playback_t *player);
void video_playback_pause(video_playback_t *player, int paused);
void video_playback_stop(video_playback_t *player);

#endif
