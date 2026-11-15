#ifndef PHOTO_AUDIO_CONTROL_H
#define PHOTO_AUDIO_CONTROL_H

#include <stddef.h>

int audio_volume_load(void);
int audio_volume_save(int percent);
int audio_volume_apply(int percent);
int audio_volume_current(void);
void audio_output_device(char *device, size_t capacity);
/* Poll USB headset media keys; returns the new volume, or -1 if unchanged. */
int audio_input_poll(void);

#endif
