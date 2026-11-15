#include "media_server.h"
#include "camera_backend.h"
#include "image_convert.h"
#include "image_settings.h"
#include "video_backend.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAX_UPLOAD (64 * 1024 * 1024)
#define MAX_VIDEO_UPLOAD ((size_t)2 * 1024 * 1024 * 1024)
#define REQUEST_SIZE 8192
#define PAGE_PATH "/usr/share/photo-viewer/index.html"

static char photo_directory[384];
static char video_directory[384];
static atomic_int media_changed;
static pthread_t server_thread;
static struct pollfd server_listeners[2];
static pthread_mutex_t camera_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t *camera_frame;
static size_t camera_frame_size;
static int camera_streaming;
static atomic_int active_clients;

#define MAX_CLIENTS 16

static int file_path(const char *name, char *path, size_t capacity);


static int send_all(int fd, const void *data, size_t size)
{
    const char *cursor = data;
    while(size) {
        ssize_t sent = send(fd, cursor, size, MSG_NOSIGNAL);
        if(sent <= 0) return 0;
        cursor += sent;
        size -= (size_t)sent;
    }
    return 1;
}

static void reply(int fd, int code, const char *reason, const char *type,
                  const void *data, size_t size)
{
    char header[256];
    int length = snprintf(header, sizeof(header),
                          "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                          "Connection: close\r\nCache-Control: no-store\r\n\r\n",
                          code, reason, type, size);
    if(length > 0 && (size_t)length < sizeof(header) &&
       send_all(fd, header, (size_t)length) && data && size) send_all(fd, data, size);
}

static void error_reply(int fd, int code, const char *reason, const char *message)
{
    reply(fd, code, reason, "text/plain; charset=utf-8", message, strlen(message));
}

static int camera_mode_choice(const camera_mode_t *modes, int count)
{
    uint32_t width = 0, height = 0, format = 0;
    int saved = camera_settings_read(&width, &height, &format);
    int choice = 0;
    for(int i = 0; i < count; i++) {
        if(saved && modes[i].width == width && modes[i].height == height &&
           (!format || modes[i].format == format)) return i;
        if(!saved && modes[i].width == 640 && modes[i].height == 480) {
            choice = i; break;
        }
    }
    return choice;
}

static void camera_status(int fd)
{
    pthread_mutex_lock(&camera_lock);
    int streaming = camera_streaming;
    pthread_mutex_unlock(&camera_lock);
    const char *json = streaming ? "{\"streaming\":true}" : "{\"streaming\":false}";
    reply(fd, 200, "OK", "application/json", json, strlen(json));
}

static void list_camera_modes(int fd)
{
    camera_mode_t modes[CAMERA_MAX_MODES];
    char path[64], name[64];
    int count = camera_discover(path, sizeof(path), name, sizeof(name),
                                modes, CAMERA_MAX_MODES);
    if(count <= 0) {
        error_reply(fd, 503, "Service Unavailable", "No supported USB camera found");
        return;
    }
    char json[8192];
    size_t used = 0;
    int length = snprintf(json, sizeof(json), "{\"selected\":%d,\"modes\":[",
                          camera_mode_choice(modes, count));
    if(length < 0 || (size_t)length >= sizeof(json)) goto overflow;
    used = (size_t)length;
    for(int i = 0; i < count; i++) {
        length = snprintf(json + used, sizeof(json) - used,
                          "%s{\"width\":%u,\"height\":%u,\"format\":%u,\"name\":\"%s\"}",
                          i ? "," : "", modes[i].width, modes[i].height,
                          modes[i].format, camera_format_name(modes[i].format));
        if(length < 0 || (size_t)length >= sizeof(json) - used - 2) goto overflow;
        used += (size_t)length;
    }
    json[used++] = ']'; json[used++] = '}';
    reply(fd, 200, "OK", "application/json", json, used);
    return;
overflow:
    error_reply(fd, 500, "Internal Server Error", "Camera mode list is too large");
}

static void set_camera_mode(int fd, const char *target)
{
    unsigned int width = 0, height = 0, format = 0;
    int end = 0;
    if(sscanf(target, "/api/camera/settings?width=%u&height=%u&format=%u%n",
              &width, &height, &format, &end) != 3 || !end || target[end]) {
        error_reply(fd, 400, "Bad Request", "Invalid camera mode");
        return;
    }
    camera_mode_t modes[CAMERA_MAX_MODES];
    char path[64], name[64];
    int count = camera_discover(path, sizeof(path), name, sizeof(name),
                                modes, CAMERA_MAX_MODES);
    if(count <= 0) {
        error_reply(fd, 503, "Service Unavailable", "No supported USB camera found");
        return;
    }
    for(int i = 0; i < count; i++) {
        if(modes[i].width == width && modes[i].height == height &&
           modes[i].format == format) {
            if(camera_settings_write(&modes[i])) error_reply(fd, 200, "OK", "Camera settings saved");
            else error_reply(fd, 500, "Internal Server Error", "Could not save camera settings");
            return;
        }
    }
    error_reply(fd, 400, "Bad Request", "Camera does not support that mode");
}

static void stream_camera(int fd)
{
    pthread_mutex_lock(&camera_lock);
    if(camera_streaming) {
        pthread_mutex_unlock(&camera_lock);
        error_reply(fd, 409, "Conflict", "Camera preview is already open in another browser");
        return;
    }
    camera_streaming = 1;
    pthread_mutex_unlock(&camera_lock);

    camera_mode_t modes[CAMERA_MAX_MODES];
    char path[64], name[64], error[128] = {0};
    int count = camera_discover(path, sizeof(path), name, sizeof(name),
                                modes, CAMERA_MAX_MODES);
    camera_mode_t mode = count > 0 ? modes[camera_mode_choice(modes, count)] :
                                      (camera_mode_t){0};
    camera_device_t *camera = count > 0 ?
        camera_start(path, &mode, error, sizeof(error)) : NULL;
    if(!camera) {
        error_reply(fd, 503, "Service Unavailable", count ?
                    (error[0] ? error : "Could not start camera") :
                    "No supported USB camera found");
        goto done;
    }
    struct timeval timeout = {.tv_sec = 2};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    const char header[] = "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                          "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
    if(!send_all(fd, header, sizeof(header) - 1)) goto done;
    for(;;) {
        char probe;
        ssize_t connection = recv(fd, &probe, 1, MSG_PEEK | MSG_DONTWAIT);
        if(connection == 0 || (connection < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) break;
        int ready = camera_wait(camera, 1000);
        if(ready < 0) break;
        if(!ready || camera_poll(camera, NULL, 0, 0) != 1) continue;
        uint8_t *frame = NULL;
        size_t size = 0;
        if(!camera_capture_jpeg(camera, &frame, &size)) continue;
        pthread_mutex_lock(&camera_lock);
        free(camera_frame);
        camera_frame = frame;
        camera_frame_size = size;
        pthread_mutex_unlock(&camera_lock);
        char part[128];
        int length = snprintf(part, sizeof(part),
                              "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", size);
        if(length <= 0 || (size_t)length >= sizeof(part) ||
           !send_all(fd, part, (size_t)length) || !send_all(fd, frame, size) ||
           !send_all(fd, "\r\n", 2)) break;
    }
done:
    camera_stop(camera);
    pthread_mutex_lock(&camera_lock);
    free(camera_frame);
    camera_frame = NULL;
    camera_frame_size = 0;
    camera_streaming = 0;
    pthread_mutex_unlock(&camera_lock);
}

static void capture_camera(int fd)
{
    pthread_mutex_lock(&camera_lock);
    size_t size = camera_frame_size;
    uint8_t *frame = size ? malloc(size) : NULL;
    if(frame) memcpy(frame, camera_frame, size);
    int streaming = camera_streaming;
    pthread_mutex_unlock(&camera_lock);
    if(!frame) {
        error_reply(fd, streaming ? 503 : 409, streaming ? "Service Unavailable" : "Conflict",
                    streaming ? "Waiting for a camera frame" : "Open Live camera first");
        return;
    }
    time_t now = time(NULL);
    struct tm local;
    char stamp[32];
    if(localtime_r(&now, &local)) strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &local);
    else snprintf(stamp, sizeof(stamp), "%ld", (long)now);
    char name[80], path[512];
    int output = -1;
    for(unsigned int attempt = 0; attempt < 1000; attempt++) {
        snprintf(name, sizeof(name), "Photo_%s_%03u.jpg", stamp, attempt);
        if(!file_path(name, path, sizeof(path))) break;
        output = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if(output >= 0 || errno != EEXIST) break;
    }
    if(output < 0) {
        free(frame);
        error_reply(fd, 500, "Internal Server Error", "Could not create photo in gallery");
        return;
    }
    size_t written = 0;
    while(written < size) {
        ssize_t count = write(output, frame + written, size - written);
        if(count <= 0) break;
        written += (size_t)count;
    }
    int okay = written == size && fsync(output) == 0;
    if(close(output)) okay = 0;
    if(!okay) {
        unlink(path);
        free(frame);
        error_reply(fd, 500, "Internal Server Error", "Could not save photo in gallery");
        return;
    }
    atomic_store(&media_changed, 1);
    char header[384];
    int length = snprintf(header, sizeof(header),
                          "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n"
                          "Content-Disposition: attachment; filename=\"%s\"\r\n"
                          "X-Photo-Name: %s\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                          size, name, name);
    if(length > 0 && (size_t)length < sizeof(header) && send_all(fd, header, (size_t)length))
        send_all(fd, frame, size);
    free(frame);
}

static void serve_page(int fd)
{
    FILE *stream = fopen(PAGE_PATH, "rb");
    if(!stream) { error_reply(fd, 500, "Internal Server Error", "page unavailable"); return; }
    struct stat info;
    if(fstat(fileno(stream), &info) || info.st_size > 65536 || info.st_size < 1) {
        fclose(stream);
        error_reply(fd, 500, "Internal Server Error", "page unavailable");
        return;
    }
    char *content = malloc((size_t)info.st_size);
    if(!content) { fclose(stream); error_reply(fd, 500, "Internal Server Error", "memory error"); return; }
    size_t size = fread(content, 1, (size_t)info.st_size, stream);
    fclose(stream);
    reply(fd, 200, "OK", "text/html; charset=utf-8", content, size);
    free(content);
}

static int safe_name(const char *name, int video)
{
    size_t length = strlen(name);
    if(length < (video ? 4u : 5u) || length > 120 ||
       !(video ? video_supported_name(name) : image_supported_name(name))) return 0;
    if(name[0] == '.' || strstr(name, "..")) return 0;
    for(size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)name[i];
        if(!(isalnum(c) || strchr(" ._-()[]'!~", c))) return 0;
    }
    return 1;
}

static int decode_name(const char *target, const char *key, char *name,
                       size_t capacity, int video)
{
    const char *query = strchr(target, '?');
    if(!query) return 0;
    const char *source = query + 1;
    size_t key_length = strlen(key);
    while(*source) {
        if(!strncmp(source, key, key_length) && source[key_length] == '=') {
            source += key_length + 1;
            break;
        }
        source = strchr(source, '&');
        if(!source) return 0;
        source++;
    }
    if(!*source) return 0;
    size_t used = 0;
    while(*source && *source != '&') {
        unsigned char c = (unsigned char)*source++;
        if(c == '%') {
            int a, b;
            if(!isxdigit((unsigned char)source[0]) || !isxdigit((unsigned char)source[1])) return 0;
            a = isdigit((unsigned char)source[0]) ? source[0] - '0' : tolower((unsigned char)source[0]) - 'a' + 10;
            b = isdigit((unsigned char)source[1]) ? source[1] - '0' : tolower((unsigned char)source[1]) - 'a' + 10;
            c = (unsigned char)((a << 4) | b);
            source += 2;
        } else if(c == '+') c = ' ';
        if(c == 0 || used + 1 >= capacity) return 0;
        name[used++] = (char)c;
    }
    name[used] = '\0';
    return safe_name(name, video);
}

static int media_path(const char *name, char *path, size_t capacity, int video)
{
    int length = snprintf(path, capacity, "%s/%s",
                          video ? video_directory : photo_directory, name);
    return length > 0 && (size_t)length < capacity;
}

static int file_path(const char *name, char *path, size_t capacity)
{
    return media_path(name, path, capacity, 0);
}

static void list_files(int fd, int video)
{
    DIR *dir = opendir(video ? video_directory : photo_directory);
    if(!dir) { error_reply(fd, 500, "Internal Server Error", "media folder unavailable"); return; }
    char json[16384];
    size_t used = 0;
    json[used++] = '[';
    struct dirent *entry;
    while((entry = readdir(dir))) {
        char path[512];
        struct stat st;
        if(!safe_name(entry->d_name, video) ||
           !media_path(entry->d_name, path, sizeof(path), video) ||
           lstat(path, &st) || !S_ISREG(st.st_mode)) continue;
        /* Filenames accepted by safe_name contain no JSON control characters or quotes. */
        int length = snprintf(json + used, sizeof(json) - used, "%s{\"name\":\"%s\",\"size\":%lld}",
                              used > 1 ? "," : "", entry->d_name, (long long)st.st_size);
        if(length < 0 || (size_t)length >= sizeof(json) - used - 2) break;
        used += (size_t)length;
    }
    closedir(dir);
    json[used++] = ']';
    reply(fd, 200, "OK", "application/json", json, used);
}

static void download_file(int fd, const char *path)
{
    FILE *stream = fopen(path, "rb");
    struct stat st;
    if(!stream || fstat(fileno(stream), &st) || !S_ISREG(st.st_mode)) {
        if(stream) fclose(stream);
        error_reply(fd, 404, "Not Found", "file not found");
        return;
    }
    char header[256];
    int length = snprintf(header, sizeof(header),
                          "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %lld\r\n"
                          "Connection: close\r\n\r\n", (long long)st.st_size);
    if(length > 0 && (size_t)length < sizeof(header) && send_all(fd, header, (size_t)length)) {
        char buffer[8192];
        size_t count;
        while((count = fread(buffer, 1, sizeof(buffer), stream)) > 0)
            if(!send_all(fd, buffer, count)) break;
    }
    fclose(stream);
}

static void stream_video(int fd, const char *path, const char *request, const char *body)
{
    int input = open(path, O_RDONLY | O_NOFOLLOW);
    struct stat info;
    if(input < 0 || fstat(input, &info) || !S_ISREG(info.st_mode)) {
        if(input >= 0) close(input);
        error_reply(fd, 404, "Not Found", "video not found");
        return;
    }
    off_t start = 0, end = info.st_size - 1;
    int partial = 0;
    const char *line = strstr(request, "\r\n");
    while(line && line < body) {
        line += 2;
        if(!strncasecmp(line, "Range: bytes=", 13)) {
            char *after;
            errno = 0;
            unsigned long long first = strtoull(line + 13, &after, 10);
            if(errno || after == line + 13 || (*after != '-' && *after != '\r')) {
                close(input); error_reply(fd, 416, "Range Not Satisfiable", "invalid range"); return;
            }
            start = (off_t)first;
            if(*after == '-' && isdigit((unsigned char)after[1])) {
                unsigned long long last = strtoull(after + 1, &after, 10);
                if(last < (unsigned long long)end) end = (off_t)last;
            }
            partial = 1;
            break;
        }
        line = strstr(line, "\r\n");
    }
    if(start < 0 || start >= info.st_size || end < start) {
        close(input); error_reply(fd, 416, "Range Not Satisfiable", "invalid range"); return;
    }
    const char *ext = strrchr(path, '.');
    const char *type = ext && !strcasecmp(ext, ".webm") ? "video/webm" :
                       ext && !strcasecmp(ext, ".mkv") ? "video/x-matroska" :
                       ext && !strcasecmp(ext, ".mov") ? "video/quicktime" :
                       ext && !strcasecmp(ext, ".avi") ? "video/x-msvideo" :
                       ext && (!strcasecmp(ext, ".mpeg") || !strcasecmp(ext, ".mpg")) ? "video/mpeg" :
                       ext && !strcasecmp(ext, ".ts") ? "video/mp2t" :
                       ext && !strcasecmp(ext, ".3gp") ? "video/3gpp" :
                       ext && !strcasecmp(ext, ".wmv") ? "video/x-ms-wmv" :
                       ext && !strcasecmp(ext, ".flv") ? "video/x-flv" : "video/mp4";
    char header[512];
    int size;
    if(partial) {
        size = snprintf(header, sizeof(header),
            "HTTP/1.1 206 Partial Content\r\nContent-Type: %s\r\nAccept-Ranges: bytes\r\n"
            "Content-Length: %lld\r\nContent-Range: bytes %lld-%lld/%lld\r\n"
            "Connection: close\r\n\r\n", type, (long long)(end - start + 1),
            (long long)start, (long long)end, (long long)info.st_size);
    } else size = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nAccept-Ranges: bytes\r\n"
        "Content-Length: %lld\r\nConnection: close\r\n\r\n",
        type, (long long)info.st_size);
    if(size <= 0 || (size_t)size >= sizeof(header) || !send_all(fd, header, (size_t)size)) {
        close(input); return;
    }
    if(lseek(input, start, SEEK_SET) >= 0) {
        char buffer[65536];
        off_t left = end - start + 1;
        while(left > 0) {
            size_t wanted = left < (off_t)sizeof(buffer) ? (size_t)left : sizeof(buffer);
            ssize_t got = read(input, buffer, wanted);
            if(got <= 0 || !send_all(fd, buffer, (size_t)got)) break;
            left -= got;
        }
    }
    close(input);
}

static void video_settings_api(int fd, const char *method, const char *target)
{
    char name[128], path[512], json[128];
    if(!decode_name(target, "name", name, sizeof(name), 1) ||
       !media_path(name, path, sizeof(path), 1)) {
        error_reply(fd, 400, "Bad Request", "invalid video name"); return;
    }
    struct stat info;
    if(lstat(path, &info) || !S_ISREG(info.st_mode)) {
        error_reply(fd, 404, "Not Found", "video not found"); return;
    }
    image_settings_t settings;
    image_settings_load(path, &settings);
    if(!strcmp(method, "POST")) {
        const char *angle = strstr(target, "&rotation=");
        const char *mode = strstr(target, "&mode=");
        unsigned int rotation;
        if(!angle || !mode || sscanf(angle + 10, "%u", &rotation) != 1 ||
           rotation > 270 || rotation % 90 ||
           (strncmp(mode + 6, "fit", 3) && strncmp(mode + 6, "fill", 4))) {
            error_reply(fd, 400, "Bad Request", "invalid video settings"); return;
        }
        settings.rotation = rotation;
        settings.mode = !strncmp(mode + 6, "fill", 4) ? IMAGE_FILL_DISPLAY : IMAGE_FIT_WHOLE;
        if(!image_settings_save(path, &settings)) {
            error_reply(fd, 500, "Internal Server Error", "could not save video settings"); return;
        }
    }
    int size = snprintf(json, sizeof(json), "{\"rotation\":%u,\"mode\":\"%s\"}",
                        settings.rotation, settings.mode == IMAGE_FILL_DISPLAY ? "fill" : "fit");
    reply(fd, 200, "OK", "application/json", json, (size_t)size);
}

static void upload_file(int fd, const char *path, const char *body, size_t buffered,
                        size_t content_length, int video)
{
    if(content_length < 1 || content_length > (video ? MAX_VIDEO_UPLOAD : MAX_UPLOAD) ||
       buffered > content_length) {
        error_reply(fd, 400, "Bad Request", "invalid upload size");
        return;
    }
    char temporary[544];
    if(snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path) >= (int)sizeof(temporary)) {
        error_reply(fd, 400, "Bad Request", "file name too long");
        return;
    }
    int tmpfd = mkstemp(temporary);
    if(tmpfd < 0) { error_reply(fd, 500, "Internal Server Error", "could not create file"); return; }
    FILE *stream = fdopen(tmpfd, "w+b");
    if(!stream) { close(tmpfd); unlink(temporary); error_reply(fd, 500, "Internal Server Error", "write failed"); return; }
    size_t received = buffered;
    int ok = fwrite(body, 1, buffered, stream) == buffered;
    while(ok && received < content_length) {
        char buffer[8192];
        size_t wanted = content_length - received;
        if(wanted > sizeof(buffer)) wanted = sizeof(buffer);
        ssize_t count = recv(fd, buffer, wanted, 0);
        if(count <= 0) { ok = 0; break; }
        ok = fwrite(buffer, 1, (size_t)count, stream) == (size_t)count;
        received += (size_t)count;
    }
    if(ok) ok = fflush(stream) == 0;
    if(fclose(stream)) ok = 0;
    if(ok && !video) {
        char converted[128];
        ok = image_convert_to_bmp(temporary, path, converted, sizeof(converted));
        if(ok) unlink(converted);
    }
    if(ok && video) ok = video_probe_file(temporary);
    if(!ok) {
        unlink(temporary);
        error_reply(fd, 400, "Bad Request", "upload failed or media format unsupported");
        return;
    }
    if(rename(temporary, path)) {
        unlink(temporary);
        error_reply(fd, 500, "Internal Server Error", "could not save file");
        return;
    }
    atomic_store(&media_changed, 1);
    error_reply(fd, 200, "OK", "uploaded");
}

static void move_or_copy(int fd, const char *target, int copy, int video)
{
    char from[128], to[128], source[512], destination[512];
    if(!decode_name(target, "from", from, sizeof(from), video) ||
       !decode_name(target, "to", to, sizeof(to), video) ||
       !media_path(from, source, sizeof(source), video) ||
       !media_path(to, destination, sizeof(destination), video)) {
        error_reply(fd, 400, "Bad Request", "invalid file name");
        return;
    }
    struct stat info;
    if(!lstat(destination, &info)) {
        error_reply(fd, 409, "Conflict", "destination already exists");
        return;
    }
    if(lstat(source, &info) || !S_ISREG(info.st_mode)) {
        error_reply(fd, 404, "Not Found", "source file not found");
        return;
    }
    if(!copy) {
        if(rename(source, destination)) {
            error_reply(fd, 500, "Internal Server Error", "rename failed");
            return;
        }
    } else {
        FILE *input = fopen(source, "rb");
        char temporary[544];
        if(!input || snprintf(temporary, sizeof(temporary), "%s.XXXXXX", destination) >= (int)sizeof(temporary)) {
            if(input) fclose(input);
            error_reply(fd, 500, "Internal Server Error", "copy failed");
            return;
        }
        int descriptor = mkstemp(temporary);
        if(descriptor < 0) {
            fclose(input);
            error_reply(fd, 500, "Internal Server Error", "copy failed");
            return;
        }
        FILE *output = fdopen(descriptor, "wb");
        int ok = output != NULL;
        char buffer[8192];
        size_t count;
        while(ok && (count = fread(buffer, 1, sizeof(buffer), input)) > 0)
            ok = fwrite(buffer, 1, count, output) == count;
        if(ferror(input)) ok = 0;
        fclose(input);
        if(output && fclose(output)) ok = 0;
        if(!output) close(descriptor);
        if(!ok || rename(temporary, destination)) {
            unlink(temporary);
            error_reply(fd, 500, "Internal Server Error", "copy failed");
            return;
        }
    }
    if(!image_settings_transfer(source, destination, copy)) {
        if(copy) unlink(destination);
        else rename(destination, source);
        error_reply(fd, 500, "Internal Server Error", "could not preserve image settings");
        return;
    }
    atomic_store(&media_changed, 1);
    error_reply(fd, 200, "OK", copy ? "copied" : "renamed");
}

static void serve_client(int fd)
{
    char request[REQUEST_SIZE + 1];
    size_t used = 0;
    char *body = NULL;
    while(used < REQUEST_SIZE) {
        ssize_t count = recv(fd, request + used, REQUEST_SIZE - used, 0);
        if(count <= 0) return;
        used += (size_t)count;
        request[used] = '\0';
        body = strstr(request, "\r\n\r\n");
        if(body) break;
    }
    if(!body) { error_reply(fd, 431, "Request Header Fields Too Large", "request headers too large"); return; }
    char method[8], target[512];
    if(sscanf(request, "%7s %511s", method, target) != 2) {
        error_reply(fd, 400, "Bad Request", "invalid request"); return;
    }
    if(!strcmp(method, "GET") && !strcmp(target, "/")) {
        serve_page(fd); return;
    }
    if(!strcmp(method, "GET") && !strcmp(target, "/api/files")) { list_files(fd, 0); return; }
    if(!strcmp(method, "GET") && !strcmp(target, "/api/videos")) { list_files(fd, 1); return; }
    if((!strcmp(method, "GET") || !strcmp(method, "POST")) &&
       !strncmp(target, "/video-settings?", 16)) {
        video_settings_api(fd, method, target); return;
    }
    if(!strcmp(method, "GET") && !strcmp(target, "/api/camera/status")) { camera_status(fd); return; }
    if(!strcmp(method, "GET") && !strcmp(target, "/api/camera/settings")) { list_camera_modes(fd); return; }
    if(!strcmp(method, "POST") && !strncmp(target, "/api/camera/settings?", 21)) {
        set_camera_mode(fd, target); return;
    }
    if(!strcmp(method, "GET") &&
       (!strcmp(target, "/api/camera/stream") ||
        !strncmp(target, "/api/camera/stream?", 19))) { stream_camera(fd); return; }
    if(!strcmp(method, "POST") && !strcmp(target, "/api/camera/capture")) { capture_camera(fd); return; }
    if(!strcmp(method, "POST") && !strncmp(target, "/copy?", 6)) {
        move_or_copy(fd, target, 1, 0); return;
    }
    if(!strcmp(method, "POST") && !strncmp(target, "/rename?", 8)) {
        move_or_copy(fd, target, 0, 0); return;
    }
    if(!strcmp(method, "POST") && !strncmp(target, "/video-copy?", 12)) {
        move_or_copy(fd, target, 1, 1); return;
    }
    if(!strcmp(method, "POST") && !strncmp(target, "/video-rename?", 14)) {
        move_or_copy(fd, target, 0, 1); return;
    }
    int video = !strncmp(target, "/video-", 7);
    int upload = !strcmp(method, "PUT") && !strncmp(target, "/upload?", 8);
    int remove_file = !strcmp(method, "DELETE") && !strncmp(target, "/delete?", 8);
    int download = !strcmp(method, "GET") && !strncmp(target, "/file?", 6);
    if(video) {
        upload = !strcmp(method, "PUT") && !strncmp(target, "/video-upload?", 14);
        remove_file = !strcmp(method, "DELETE") && !strncmp(target, "/video-delete?", 14);
        download = !strcmp(method, "GET") && !strncmp(target, "/video-file?", 12);
    }
    if(!upload && !remove_file && !download) {
        error_reply(fd, 404, "Not Found", "not found"); return;
    }
    char name[128], path[512];
    if(!decode_name(target, "name", name, sizeof(name), video) ||
       !media_path(name, path, sizeof(path), video)) {
        error_reply(fd, 400, "Bad Request", "invalid file name"); return;
    }
    if(remove_file) {
        if(!unlink(path)) {
            image_settings_remove(path);
            atomic_store(&media_changed, 1);
            error_reply(fd, 200, "OK", "deleted");
        }
        else error_reply(fd, 404, "Not Found", "file not found");
        return;
    }
    if(download) {
        if(video) stream_video(fd, path, request, body);
        else download_file(fd, path);
        return;
    }
    size_t content_length = 0;
    int found_length = 0;
    char *line = strstr(request, "\r\n");
    while(line && line < body) {
        line += 2;
        if(!strncasecmp(line, "Content-Length:", 15)) {
            char *end;
            errno = 0;
            unsigned long parsed = strtoul(line + 15, &end, 10);
            if(errno || end == line + 15 || (*end != '\r' && *end != ' ') ||
               parsed > (video ? MAX_VIDEO_UPLOAD : MAX_UPLOAD)) {
                error_reply(fd, 400, "Bad Request", "invalid content length"); return;
            }
            content_length = (size_t)parsed;
            found_length = 1;
        }
        line = strstr(line, "\r\n");
    }
    if(!found_length) { error_reply(fd, 411, "Length Required", "content length required"); return; }
    body += 4;
    upload_file(fd, path, body, used - (size_t)(body - request), content_length, video);
}

static int listen_port(unsigned int port)
{
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if(listener < 0) return -1;
    int yes = 1;
    struct sockaddr_in address = {
        .sin_family = AF_INET, .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if(bind(listener, (void *)&address, sizeof(address)) || listen(listener, 4)) {
        perror("photo-viewer: bind/listen");
        close(listener);
        return -1;
    }
    return listener;
}

static void *client_main(void *argument)
{
    int fd = (int)(intptr_t)argument;
    struct timeval timeout = {.tv_sec = 10};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    serve_client(fd);
    close(fd);
    atomic_fetch_sub(&active_clients, 1);
    return NULL;
}

static void *server_main(void *unused)
{
    (void)unused;
    for(;;) {
        if(poll(server_listeners, 2, -1) <= 0) continue;
        for(size_t i = 0; i < 2; i++) {
            if(!(server_listeners[i].revents & POLLIN)) continue;
            int fd = accept(server_listeners[i].fd, NULL, NULL);
            if(fd < 0) continue;
            if(atomic_fetch_add(&active_clients, 1) >= MAX_CLIENTS) {
                atomic_fetch_sub(&active_clients, 1);
                error_reply(fd, 503, "Service Unavailable", "Too many connections");
                close(fd);
                continue;
            }
            pthread_t client;
            if(pthread_create(&client, NULL, client_main, (void *)(intptr_t)fd)) {
                atomic_fetch_sub(&active_clients, 1);
                close(fd);
            } else pthread_detach(client);
        }
    }
}

int media_server_start(const char *directory, const char *videos)
{
    if(snprintf(photo_directory, sizeof(photo_directory), "%s", directory) >= (int)sizeof(photo_directory)) return 0;
    if(snprintf(video_directory, sizeof(video_directory), "%s", videos) >= (int)sizeof(video_directory)) return 0;
    /* Bind before reporting success so systemd can restart this service when
     * neither HTTP port is available. */
    server_listeners[0] = (struct pollfd){.fd = listen_port(80), .events = POLLIN};
    server_listeners[1] = (struct pollfd){.fd = listen_port(8080), .events = POLLIN};
    if(server_listeners[0].fd < 0 && server_listeners[1].fd < 0) return 0;
    if(pthread_create(&server_thread, NULL, server_main, NULL)) {
        for(int i = 0; i < 2; i++) if(server_listeners[i].fd >= 0) close(server_listeners[i].fd);
        return 0;
    }
    pthread_detach(server_thread);
    return 1;
}

int media_server_take_changed(void)
{
    return atomic_exchange(&media_changed, 0);
}
