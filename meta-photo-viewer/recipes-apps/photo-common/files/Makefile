CPPFLAGS += $(shell pkg-config --cflags lvgl)
CPPFLAGS += $(if $(filter 1,$(PHOTO_VIEWER_7INCH)),-DPHOTO_VIEWER_7INCH,)
LDLIBS += $(shell pkg-config --libs lvgl) -ljpeg -lm
LDLIBS += $(shell pkg-config --libs sdl2)

APP_SOURCES = main.c camera_ui.c camera_catalog.c camera_ipc_client.c video_ui.c video_backend.c \
    image_convert.c image_settings.c wifi_control.c hostname_settings.c \
    audio_control.c selection_state.c
APP_HEADERS = display_layout.h camera_ui.h camera_backend.h video_ui.h \
    video_backend.h image_convert.h image_settings.h wifi_control.h \
    hostname_settings.h audio_control.h selection_state.h

all: photo-launcher photo-images photo-videos photo-settings photo-camera photo-web photo-viewer-splash

photo-launcher: launcher.c display_layout.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ launcher.c $(LDLIBS) -pthread

photo-images: ROLE = IMAGE
photo-videos: ROLE = VIDEO
photo-settings: ROLE = SETTINGS
photo-camera: ROLE = CAMERA
photo-images photo-videos photo-settings photo-camera: $(APP_SOURCES) $(APP_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -DPHOTO_APP_$(ROLE) -o $@ $(APP_SOURCES) $(LDLIBS) -pthread

photo-web: web_server_main.c media_server.c camera_catalog.c camera_ipc_client.c image_convert.c image_settings.c video_backend.c audio_control.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ web_server_main.c media_server.c camera_catalog.c camera_ipc_client.c image_convert.c image_settings.c video_backend.c audio_control.c $(LDLIBS) -pthread

photo-camera-service: camera_service.c camera_backend.c camera_catalog.c camera_ipc.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ camera_service.c camera_backend.c camera_catalog.c $(LDLIBS) -pthread

photo-viewer-splash: boot_splash.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ boot_splash.c

