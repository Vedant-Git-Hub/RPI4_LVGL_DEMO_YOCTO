#ifndef MEDIA_SERVER_H
#define MEDIA_SERVER_H

int media_server_start(const char *photo_directory, const char *video_directory);
int media_server_take_changed(void);

#endif
