#include "video_backend.h"
#include "audio_control.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct video_playback {
    pthread_t thread;
    pthread_t audio_thread;
    pthread_mutex_t lock;
    int read_fd, audio_read_fd, audio_write_fd, audio_thread_started;
    pid_t video_pid, audio_pid, speaker_pid;
    uint8_t *front, *back;
    size_t frame_size;
    uint64_t sequence;
    int finished, stopping, paused;
    double start_seconds, paused_at, pause_total;
    struct timespec started, pause_started;
};

static double elapsed_since(const struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start->tv_sec) +
           (double)(now.tv_nsec - start->tv_nsec) / 1000000000.0;
}

int video_supported_name(const char *name)
{
    const char *extension = strrchr(name, '.');
    if(!extension || extension == name) return 0;
    const char *formats[] = {".mp4", ".mkv", ".mov", ".webm", ".avi",
                             ".m4v", ".mpeg", ".mpg", ".ts", ".3gp",
                             ".wmv", ".flv"};
    for(size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++)
        if(!strcasecmp(extension, formats[i])) return 1;
    return 0;
}

int video_probe_file(const char *path)
{
    if(!path) return 0;
    int output[2];
    if(pipe(output)) return 0;
    pid_t child = fork();
    if(child == 0) {
        close(output[0]);
        dup2(output[1], STDOUT_FILENO);
        close(output[1]);
        int errors = open("/dev/null", O_WRONLY);
        if(errors >= 0) { dup2(errors, STDERR_FILENO); close(errors); }
        char *const arguments[] = {"ffprobe", "-v", "error", "-select_streams", "v:0",
                                   "-show_entries", "stream=codec_name", "-of", "csv=p=0",
                                   (char *)path, NULL};
        execv("/usr/bin/ffprobe", arguments);
        _exit(127);
    }
    close(output[1]);
    if(child < 0) { close(output[0]); return 0; }
    char result[64];
    ssize_t count = read(output[0], result, sizeof(result));
    close(output[0]);
    int status = 0;
    while(waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return count > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

double video_probe_duration(const char *path)
{
    if(!path) return 0;
    int output[2];
    if(pipe(output)) return 0;
    pid_t child = fork();
    if(child == 0) {
        close(output[0]);
        dup2(output[1], STDOUT_FILENO);
        close(output[1]);
        char *const arguments[] = {"ffprobe", "-v", "error", "-show_entries",
                                   "format=duration", "-of", "default=nw=1:nk=1",
                                   (char *)path, NULL};
        execv("/usr/bin/ffprobe", arguments);
        _exit(127);
    }
    close(output[1]);
    if(child < 0) { close(output[0]); return 0; }
    char result[64] = {0};
    ssize_t count = read(output[0], result, sizeof(result) - 1);
    close(output[0]);
    int status = 0;
    while(waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    if(count <= 0 || !WIFEXITED(status) || WEXITSTATUS(status)) return 0;
    char *end;
    double duration = strtod(result, &end);
    return end != result && duration > 0 && duration < 86400 ? duration : 0;
}

static pid_t spawn_video(const char *path, const char *filter, double start_seconds,
                         int *read_fd)
{
    int output[2];
    if(pipe(output)) return -1;
    pid_t child = fork();
    if(child == 0) {
        close(output[0]);
        dup2(output[1], STDOUT_FILENO);
        close(output[1]);
        int errors = open("/dev/null", O_WRONLY);
        if(errors >= 0) { dup2(errors, STDERR_FILENO); close(errors); }
        char start[32];
        snprintf(start, sizeof(start), "%.3f", start_seconds);
        char *const arguments[] = {"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
                                   "-ss", start, "-re", "-i", (char *)path, "-map", "0:v:0", "-an",
                                   "-vf", (char *)filter, "-pix_fmt", "bgr24",
                                   "-f", "rawvideo", "pipe:1", NULL};
        execv("/usr/bin/ffmpeg", arguments);
        _exit(127);
    }
    close(output[1]);
    if(child < 0) { close(output[0]); return -1; }
    *read_fd = output[0];
    return child;
}

static void *relay_audio(void *argument)
{
    video_playback_t *player = argument;
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &signals, NULL);
    /* FFmpeg emits signed 16-bit stereo PCM. Scale samples before aplay so
     * USB headsets without hardware mixer controls have a live volume. */
    unsigned char data[8192];
    size_t buffered = 0;
    ssize_t count;
    for(;;) {
        count = read(player->audio_read_fd, data + buffered,
                     sizeof(data) - buffered);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) break;
        size_t samples = (buffered + (size_t)count) & ~(size_t)1;
        int volume = audio_volume_current();
        for(size_t i = 0; i < samples; i += 2) {
            int16_t sample = (int16_t)((unsigned int)data[i] |
                                       ((unsigned int)data[i + 1] << 8));
            int16_t scaled = (int16_t)((int32_t)sample * volume / 100);
            data[i] = (unsigned char)scaled;
            data[i + 1] = (unsigned char)((uint16_t)scaled >> 8);
        }
        size_t sent = 0;
        while(sent < samples) {
            ssize_t written = write(player->audio_write_fd, data + sent,
                                    samples - sent);
            if(written < 0 && errno == EINTR) continue;
            if(written <= 0) goto finished;
            sent += (size_t)written;
        }
        buffered = buffered + (size_t)count - samples;
        if(buffered) data[0] = data[samples];
    }
finished:
    close(player->audio_read_fd);
    close(player->audio_write_fd);
    player->audio_read_fd = player->audio_write_fd = -1;
    return NULL;
}

static int spawn_audio(const char *path, double start_seconds,
                       video_playback_t *player)
{
    int decoded[2], playback[2];
    if(pipe(decoded)) return 0;
    if(pipe(playback)) { close(decoded[0]); close(decoded[1]); return 0; }
    char device[64];
    audio_output_device(device, sizeof(device));
    fprintf(stderr, "photo-viewer: audio output %s\n", device);
    player->speaker_pid = fork();
    if(player->speaker_pid == 0) {
        close(decoded[0]); close(decoded[1]);
        close(playback[1]);
        dup2(playback[0], STDIN_FILENO);
        close(playback[0]);
        /* Keep ALSA errors in the service journal for headphone diagnosis. */
        char *const arguments[] = {"aplay", "-q", "-D", device, "-f", "S16_LE",
                                   "-c", "2", "-r", "48000", NULL};
        execv("/usr/bin/aplay", arguments);
        _exit(127);
    }
    if(player->speaker_pid < 0) goto fail;
    player->audio_pid = fork();
    if(player->audio_pid == 0) {
        close(playback[0]); close(playback[1]);
        close(decoded[0]);
        dup2(decoded[1], STDOUT_FILENO);
        close(decoded[1]);
        /* Keep decoder errors in the service journal as well. */
        char start[32];
        snprintf(start, sizeof(start), "%.3f", start_seconds);
        char *const arguments[] = {"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
                                   "-ss", start, "-i", (char *)path, "-map", "0:a:0?", "-vn", "-ac", "2",
                                   "-ar", "48000", "-f", "s16le", "pipe:1", NULL};
        execv("/usr/bin/ffmpeg", arguments);
        _exit(127);
    }
    close(decoded[1]); close(playback[0]);
    if(player->audio_pid < 0) {
        close(decoded[0]); close(playback[1]);
        kill(player->speaker_pid, SIGTERM);
        waitpid(player->speaker_pid, NULL, 0);
        player->speaker_pid = -1;
        return 0;
    }
    player->audio_read_fd = decoded[0];
    player->audio_write_fd = playback[1];
    if(pthread_create(&player->audio_thread, NULL, relay_audio, player)) {
        kill(player->audio_pid, SIGTERM);
        kill(player->speaker_pid, SIGTERM);
        close(player->audio_read_fd); close(player->audio_write_fd);
        player->audio_read_fd = player->audio_write_fd = -1;
        waitpid(player->audio_pid, NULL, 0);
        waitpid(player->speaker_pid, NULL, 0);
        player->audio_pid = player->speaker_pid = -1;
        return 0;
    }
    player->audio_thread_started = 1;
    return 1;
fail:
    close(decoded[0]); close(decoded[1]);
    close(playback[0]); close(playback[1]);
    return 0;
}

static void *read_video(void *argument)
{
    video_playback_t *player = argument;
    for(;;) {
        size_t used = 0;
        while(used < player->frame_size) {
            ssize_t count = read(player->read_fd, player->back + used,
                                 player->frame_size - used);
            if(count < 0 && errno == EINTR) continue;
            if(count <= 0) goto finished;
            used += (size_t)count;
        }
        pthread_mutex_lock(&player->lock);
        if(player->stopping) { pthread_mutex_unlock(&player->lock); break; }
        uint8_t *previous = player->front;
        player->front = player->back;
        player->back = previous;
        player->sequence++;
        pthread_mutex_unlock(&player->lock);
    }
finished:
    pthread_mutex_lock(&player->lock);
    player->finished = 1;
    pthread_mutex_unlock(&player->lock);
    return NULL;
}

static int make_filter(char *output, size_t capacity, unsigned int width,
                       unsigned int height, const image_settings_t *settings)
{
    const char *rotation = settings->rotation == 90 ? "transpose=clock," :
                           settings->rotation == 180 ? "hflip,vflip," :
                           settings->rotation == 270 ? "transpose=cclock," : "";
    const char *mode = settings->mode == IMAGE_FILL_DISPLAY ? "increase" : "decrease";
    const char *finish = settings->mode == IMAGE_FILL_DISPLAY ?
                         "crop=%u:%u:(iw-ow)/2:(ih-oh)/2" :
                         "pad=%u:%u:(ow-iw)/2:(oh-ih)/2:black";
    char tail[96];
    int tail_length = snprintf(tail, sizeof(tail), finish, width, height);
    int length = snprintf(output, capacity,
                          "fps=25,%sscale=%u:%u:force_original_aspect_ratio=%s,%s",
                          rotation, width, height, mode, tail);
    return tail_length > 0 && (size_t)tail_length < sizeof(tail) &&
           length > 0 && (size_t)length < capacity;
}

int video_render_preview(const char *path, unsigned int width, unsigned int height,
                         const image_settings_t *settings, uint8_t *pixels,
                         size_t capacity)
{
    size_t needed = (size_t)width * height * 3;
    char filter[256];
    if(!path || !settings || !pixels || !width || !height ||
       needed > capacity || !make_filter(filter, sizeof(filter), width, height, settings)) return 0;
    int output[2];
    if(pipe(output)) return 0;
    pid_t child = fork();
    if(child == 0) {
        close(output[0]);
        dup2(output[1], STDOUT_FILENO);
        close(output[1]);
        int errors = open("/dev/null", O_WRONLY);
        if(errors >= 0) { dup2(errors, STDERR_FILENO); close(errors); }
        char *const arguments[] = {"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
                                   "-i", (char *)path, "-map", "0:v:0", "-an",
                                   "-vf", filter, "-frames:v", "1", "-pix_fmt", "bgr24",
                                   "-f", "rawvideo", "pipe:1", NULL};
        execv("/usr/bin/ffmpeg", arguments);
        _exit(127);
    }
    close(output[1]);
    if(child < 0) { close(output[0]); return 0; }
    size_t used = 0;
    while(used < needed) {
        ssize_t count = read(output[0], pixels + used, needed - used);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) break;
        used += (size_t)count;
    }
    close(output[0]);
    int status = 0;
    while(waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return used == needed && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

video_playback_t *video_playback_start(const char *path, unsigned int width,
                                      unsigned int height,
                                      const image_settings_t *settings,
                                      double start_seconds,
                                      char *error, size_t error_size)
{
    if(!path || !settings || !width || !height ||
       start_seconds < 0 || (uint64_t)width * height > 1920u * 1080u) return NULL;
    char filter[256];
    if(!make_filter(filter, sizeof(filter), width, height, settings)) return NULL;
    video_playback_t *player = calloc(1, sizeof(*player));
    if(!player) return NULL;
    player->read_fd = player->audio_read_fd = player->audio_write_fd = -1;
    player->video_pid = player->audio_pid = player->speaker_pid = -1;
    player->frame_size = (size_t)width * height * 3;
    player->start_seconds = start_seconds;
    player->front = malloc(player->frame_size);
    player->back = malloc(player->frame_size);
    if(!player->front || !player->back) goto fail;
    memset(player->front, 0, player->frame_size);
    pthread_mutex_init(&player->lock, NULL);
    audio_volume_apply(audio_volume_load());
    player->video_pid = spawn_video(path, filter, start_seconds, &player->read_fd);
    if(player->video_pid < 0) goto fail_locked;
    /* Audio is optional: video continues when the Pi has no audio output. */
    spawn_audio(path, start_seconds, player);
    if(pthread_create(&player->thread, NULL, read_video, player)) goto fail_locked;
    clock_gettime(CLOCK_MONOTONIC, &player->started);
    return player;
fail_locked:
    if(player->video_pid > 0) { kill(player->video_pid, SIGTERM); waitpid(player->video_pid, NULL, 0); }
    if(player->audio_pid > 0) { kill(player->audio_pid, SIGTERM); waitpid(player->audio_pid, NULL, 0); }
    if(player->speaker_pid > 0) { kill(player->speaker_pid, SIGTERM); waitpid(player->speaker_pid, NULL, 0); }
    if(player->audio_thread_started) pthread_join(player->audio_thread, NULL);
    if(player->read_fd >= 0) close(player->read_fd);
    pthread_mutex_destroy(&player->lock);
fail:
    free(player->front); free(player->back); free(player);
    if(error && error_size) snprintf(error, error_size, "Could not start video decoder");
    return NULL;
}

double video_playback_position(video_playback_t *player)
{
    if(!player) return 0;
    double elapsed = player->paused ? player->paused_at : elapsed_since(&player->started);
    return player->start_seconds + elapsed - player->pause_total;
}

int video_playback_copy_frame(video_playback_t *player, uint8_t *destination,
                              size_t capacity, uint64_t *last_sequence)
{
    if(!player || !destination || !last_sequence || capacity < player->frame_size) return 0;
    pthread_mutex_lock(&player->lock);
    int changed = player->sequence != *last_sequence;
    if(changed) {
        memcpy(destination, player->front, player->frame_size);
        *last_sequence = player->sequence;
    }
    pthread_mutex_unlock(&player->lock);
    return changed;
}

int video_playback_finished(video_playback_t *player)
{
    if(!player) return 1;
    pthread_mutex_lock(&player->lock);
    int finished = player->finished;
    pthread_mutex_unlock(&player->lock);
    return finished;
}

void video_playback_pause(video_playback_t *player, int paused)
{
    if(!player || player->paused == !!paused) return;
    if(paused) {
        clock_gettime(CLOCK_MONOTONIC, &player->pause_started);
        player->paused_at = elapsed_since(&player->started);
    } else player->pause_total += elapsed_since(&player->pause_started);
    int signal_number = paused ? SIGSTOP : SIGCONT;
    if(player->video_pid > 0) kill(player->video_pid, signal_number);
    if(player->audio_pid > 0) kill(player->audio_pid, signal_number);
    if(player->speaker_pid > 0) kill(player->speaker_pid, signal_number);
    player->paused = !!paused;
}

void video_playback_stop(video_playback_t *player)
{
    if(!player) return;
    pthread_mutex_lock(&player->lock);
    player->stopping = 1;
    pthread_mutex_unlock(&player->lock);
    /* A stopped process must be resumed so SIGTERM can be handled. */
    if(player->paused) video_playback_pause(player, 0);
    if(player->video_pid > 0) kill(player->video_pid, SIGTERM);
    if(player->audio_pid > 0) kill(player->audio_pid, SIGTERM);
    if(player->speaker_pid > 0) kill(player->speaker_pid, SIGTERM);
    pthread_join(player->thread, NULL);
    if(player->audio_thread_started) pthread_join(player->audio_thread, NULL);
    if(player->read_fd >= 0) close(player->read_fd);
    if(player->video_pid > 0) waitpid(player->video_pid, NULL, 0);
    if(player->audio_pid > 0) waitpid(player->audio_pid, NULL, 0);
    if(player->speaker_pid > 0) waitpid(player->speaker_pid, NULL, 0);
    pthread_mutex_destroy(&player->lock);
    free(player->front); free(player->back); free(player);
}
