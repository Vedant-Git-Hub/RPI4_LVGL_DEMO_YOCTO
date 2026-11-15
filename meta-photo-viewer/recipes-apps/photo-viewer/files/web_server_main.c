#include "media_server.h"
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    if(!media_server_start("/opt/photo-viewer/photos", "/opt/photo-viewer/videos")) {
        fprintf(stderr, "photo-web: cannot start media server\n");
        return 1;
    }
    for(;;) pause();
}
