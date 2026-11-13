#include "hostname_settings.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define HOSTNAME_FILE "/etc/hostname"

void photo_hostname_current(char *name, size_t capacity)
{
    if(!name || !capacity) return;
    if(gethostname(name, capacity) || !name[0])
        snprintf(name, capacity, "%s", PHOTO_HOSTNAME_DEFAULT);
    name[capacity - 1] = '\0';
}

int photo_hostname_valid(const char *name)
{
    if(!name) return 0;
    size_t length = strlen(name);
    if(!length || length > 63 || name[0] == '-' || name[length - 1] == '-')
        return 0;
    for(size_t i = 0; i < length; i++) {
        char c = name[i];
        if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return 0;
    }
    return 1;
}

static int restart_avahi(void)
{
    pid_t child = fork();
    if(child < 0) return 0;
    if(child == 0) {
        execl("/bin/systemctl", "systemctl", "restart", "avahi-daemon.service", (char *)NULL);
        execl("/usr/bin/systemctl", "systemctl", "restart", "avahi-daemon.service", (char *)NULL);
        _exit(127);
    }
    int status;
    while(waitpid(child, &status, 0) < 0) if(errno != EINTR) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int photo_hostname_apply(const char *name)
{
    if(!photo_hostname_valid(name)) return 0;
    char temporary[] = HOSTNAME_FILE ".XXXXXX";
    int fd = mkstemp(temporary);
    if(fd < 0) return 0;
    char line[65];
    int length = snprintf(line, sizeof(line), "%s\n", name);
    int okay = fchmod(fd, 0644) == 0 &&
               write(fd, line, (size_t)length) == length && fsync(fd) == 0;
    if(close(fd)) okay = 0;
    if(okay) okay = rename(temporary, HOSTNAME_FILE) == 0;
    if(!okay) { unlink(temporary); return 0; }
    if(sethostname(name, strlen(name))) return 2;
    return restart_avahi() ? 1 : 2;
}
