#ifndef PHOTO_AUDIO_CONTROL_H
#define PHOTO_AUDIO_CONTROL_H

int audio_volume_load(void);
int audio_volume_save(int percent);
int audio_volume_apply(int percent);

#endif
