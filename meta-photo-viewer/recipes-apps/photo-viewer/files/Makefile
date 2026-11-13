CPPFLAGS += $(shell pkg-config --cflags lvgl)
LDLIBS += $(shell pkg-config --libs lvgl) -ljpeg -lm

photo-viewer: main.c media_server.c media_server.h image_convert.c image_convert.h image_settings.c image_settings.h wifi_control.c wifi_control.h camera_backend.c camera_backend.h camera_ui.c camera_ui.h hostname_settings.c hostname_settings.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ main.c media_server.c image_convert.c image_settings.c wifi_control.c camera_backend.c camera_ui.c hostname_settings.c $(LDLIBS) -pthread

