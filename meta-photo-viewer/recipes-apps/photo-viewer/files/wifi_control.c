#include "wifi_control.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int selected_network_id = -1;

static int command(const char *program, char *output, size_t capacity,
                   const char *const *args)
{
    int pipes[2];
    if(pipe(pipes)) return 0;
    pid_t child = fork();
    if(child == 0) {
        close(pipes[0]);
        dup2(pipes[1], STDOUT_FILENO);
        dup2(pipes[1], STDERR_FILENO);
        close(pipes[1]);
        execv(program, (char *const *)args);
        _exit(127);
    }
    close(pipes[1]);
    if(child < 0) { close(pipes[0]); return 0; }
    size_t used = 0;
    char buffer[256];
    ssize_t count;
    while((count = read(pipes[0], buffer, sizeof(buffer))) > 0) {
        if(output && used + 1 < capacity) {
            size_t copy = (size_t)count;
            if(copy > capacity - used - 1) copy = capacity - used - 1;
            memcpy(output + used, buffer, copy);
            used += copy;
        }
    }
    close(pipes[0]);
    if(output && capacity) output[used] = '\0';
    int status;
    return waitpid(child, &status, 0) == child && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
}

static int cli(char *output, size_t capacity, const char *a, const char *b,
               const char *c, const char *d)
{
    const char *args[] = {"wpa_cli", "-i", "wlan0", a, b, c, d, NULL};
    return command("/usr/sbin/wpa_cli", output, capacity, args);
}

static int previous_network_ids(const char *ssid, int *ids, int capacity)
{
    char output[4096];
    if(!cli(output, sizeof(output), "list_networks", NULL, NULL, NULL)) return 0;
    int total = 0;
    char *save = NULL;
    for(char *line = strtok_r(output, "\n", &save); line;
        line = strtok_r(NULL, "\n", &save)) {
        char *name = strchr(line, '\t');
        if(!name || line[0] < '0' || line[0] > '9') continue;
        *name++ = '\0';
        char *end = strchr(name, '\t');
        if(!end) continue;
        *end = '\0';
        if(!strcmp(name, ssid) && total < capacity) ids[total++] = atoi(line);
    }
    return total;
}

static int network_temporarily_disabled(const char *output, int id)
{
    char copy[4096];
    snprintf(copy, sizeof(copy), "%s", output);
    char *save = NULL;
    for(char *line = strtok_r(copy, "\n", &save); line;
        line = strtok_r(NULL, "\n", &save)) {
        char *tab = strchr(line, '\t');
        if(!tab || line[0] < '0' || line[0] > '9') continue;
        *tab = '\0';
        if((id >= 0 && atoi(line) == id) ||
           (id < 0 && strstr(tab + 1, "[CURRENT]")))
            return strstr(tab + 1, "[TEMP-DISABLED]") != NULL;
    }
    return 0;
}

static void recent_failure_reason(int id, char *reason, size_t capacity)
{
    reason[0] = '\0';
    char output[8192];
    const char *args[] = {"journalctl", "-u", "photo-viewer-wifi.service", "-b",
                          "-n", "30", "--no-pager", "-o", "cat", NULL};
    if(!command("/usr/bin/journalctl", output, sizeof(output), args)) return;
    char *last = NULL;
    for(char *event = strstr(output, "CTRL-EVENT-SSID-TEMP-DISABLED "); event;
        event = strstr(event + 1, "CTRL-EVENT-SSID-TEMP-DISABLED ")) {
        char current_id[24];
        snprintf(current_id, sizeof(current_id), "id=%d ", id);
        char *line_end = strchr(event, '\n');
        char *field = strstr(event, "reason=");
        if((id < 0 || strstr(event, current_id) == event +
            strlen("CTRL-EVENT-SSID-TEMP-DISABLED ")) &&
           field && (!line_end || field < line_end)) last = field;
    }
    if(last) (void)sscanf(last, "reason=%31[A-Z_]", reason);
    if(capacity) reason[capacity - 1] = '\0';
}

static int cli_ok(const char *a, const char *b, const char *c, const char *d)
{
    char output[256];
    return cli(output, sizeof(output), a, b, c, d) &&
           !strncmp(output, "OK", 2);
}

int wifi_scan_start(void)
{
    return cli_ok("scan", NULL, NULL, NULL);
}

int wifi_scan_results(wifi_network_t *networks, int capacity)
{
    char output[8192];
    if(!cli(output, sizeof(output), "scan_results", NULL, NULL, NULL)) return -1;
    int total = 0;
    char *save = NULL;
    for(char *line = strtok_r(output, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *ssid = strrchr(line, '\t');
        if(!ssid || !ssid[1] || total >= capacity) continue;
        ssid++;
        int duplicate = 0;
        for(int i = 0; i < total; i++)
            if(!strcmp(networks[i].ssid, ssid)) duplicate = 1;
        if(duplicate || strlen(ssid) >= sizeof(networks[total].ssid)) continue;
        snprintf(networks[total].ssid, sizeof(networks[total].ssid), "%s", ssid);
        char *signal = strchr(line, '\t');
        if(signal) signal = strchr(signal + 1, '\t');
        networks[total].signal = signal ? atoi(signal + 1) : 0;
        total++;
    }
    return total;
}

static int quoted(const char *input, char *output, size_t capacity)
{
    size_t used = 0;
    if(capacity < 3) return 0;
    output[used++] = '"';
    for(const unsigned char *p = (const unsigned char *)input; *p; p++) {
        if(*p < 32 || *p == 127 || used + 3 >= capacity) return 0;
        if(*p == '"' || *p == '\\') output[used++] = '\\';
        output[used++] = (char)*p;
    }
    output[used++] = '"';
    output[used] = '\0';
    return 1;
}

int wifi_connect(const char *ssid, const char *password)
{
    if(!ssid || !*ssid || strlen(ssid) > 32 || !password || strlen(password) > 63 ||
       (password[0] && strlen(password) < 8)) return 0;
    char network[80], secret[136], output[256];
    int previous_ids[16];
    int previous_count = previous_network_ids(ssid, previous_ids, 16);
    if(!quoted(ssid, network, sizeof(network))) return 0;
    if(password[0] && !quoted(password, secret, sizeof(secret))) return 0;
    if(!cli(output, sizeof(output), "add_network", NULL, NULL, NULL)) return 0;
    if(output[0] < '0' || output[0] > '9') return 0;
    char id[16];
    snprintf(id, sizeof(id), "%d", atoi(output));
    if(!cli_ok("set_network", id, "ssid", network)) return 0;
    if(password[0]) {
        if(!cli_ok("set_network", id, "psk", secret)) return 0;
    } else if(!cli_ok("set_network", id, "key_mgmt", "NONE")) return 0;
    if(!cli_ok("enable_network", id, NULL, NULL) ||
       !cli_ok("select_network", id, NULL, NULL)) return 0;
    selected_network_id = atoi(id);
    /* Replace older attempts for this SSID so a stale password cannot be
     * selected after a reboot. Other saved networks are preserved. */
    for(int i = 0; i < previous_count; i++) {
        char old_id[16];
        snprintf(old_id, sizeof(old_id), "%d", previous_ids[i]);
        (void)cli_ok("remove_network", old_id, NULL, NULL);
    }
    return cli_ok("save_config", NULL, NULL, NULL);
}

void wifi_status(char *message, size_t capacity)
{
    char output[2048];
    if(!cli(output, sizeof(output), "status", NULL, NULL, NULL)) {
        snprintf(message, capacity, "Wi-Fi service unavailable");
        return;
    }
    char state[32] = {0};
    const char *field = strstr(output, "wpa_state=");
    if(field) (void)sscanf(field, "wpa_state=%31s", state);
    if(strcmp(state, "COMPLETED")) {
        char networks[4096];
        if(cli(networks, sizeof(networks), "list_networks", NULL, NULL, NULL) &&
           network_temporarily_disabled(networks, selected_network_id)) {
            char reason[32];
            recent_failure_reason(selected_network_id, reason, sizeof(reason));
            if(!strcmp(reason, "WRONG_KEY"))
                snprintf(message, capacity, "Router reports wrong key; check Wi-Fi password");
            else if(reason[0])
                snprintf(message, capacity, "Wi-Fi rejected (%s); check router settings", reason);
            else
                snprintf(message, capacity, "Wi-Fi temporarily rejected; check router settings");
        }
        else if(!strcmp(state, "4WAY_HANDSHAKE") ||
           !strcmp(state, "GROUP_HANDSHAKE"))
            snprintf(message, capacity, "Wi-Fi authenticating with router...");
        else if(!strcmp(state, "ASSOCIATING") || !strcmp(state, "ASSOCIATED"))
            snprintf(message, capacity, "Wi-Fi associating with router...");
        else if(!strcmp(state, "SCANNING"))
            snprintf(message, capacity, "Wi-Fi scanning for saved network...");
        else if(!strcmp(state, "INACTIVE"))
            snprintf(message, capacity, "No Wi-Fi network selected");
        else snprintf(message, capacity, "Wi-Fi disconnected; check password or router");
        return;
    }
    struct ifaddrs *addresses;
    if(getifaddrs(&addresses)) {
        snprintf(message, capacity, "Wi-Fi connected; waiting for IP");
        return;
    }
    snprintf(message, capacity, "Wi-Fi connected; waiting for IP");
    for(struct ifaddrs *item = addresses; item; item = item->ifa_next) {
        if(item->ifa_addr && !strcmp(item->ifa_name, "wlan0") &&
           item->ifa_addr->sa_family == AF_INET) {
            char ip[INET_ADDRSTRLEN];
            struct sockaddr_in *address = (struct sockaddr_in *)item->ifa_addr;
            if(inet_ntop(AF_INET, &address->sin_addr, ip, sizeof(ip)))
                snprintf(message, capacity, "http://%s:8080", ip);
            break;
        }
    }
    freeifaddrs(addresses);
}
