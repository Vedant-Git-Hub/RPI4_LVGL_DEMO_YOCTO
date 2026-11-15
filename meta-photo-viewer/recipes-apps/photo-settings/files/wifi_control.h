#ifndef WIFI_CONTROL_H
#define WIFI_CONTROL_H

#include <stddef.h>

#define WIFI_MAX_NETWORKS 12
typedef struct {
    char ssid[64];
    int signal;
} wifi_network_t;

int wifi_scan_start(void);
int wifi_scan_results(wifi_network_t *networks, int capacity);
int wifi_connect(const char *ssid, const char *password);
void wifi_status(char *message, size_t capacity);

#endif
