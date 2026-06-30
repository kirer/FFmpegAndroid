#include <jni.h>

#include <android/log.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libavformat/avformat.h"
#include "libavutil/aes.h"
#include "libavutil/avstring.h"
#include "libavutil/error.h"
#include "libavutil/mem.h"

#define AES_BLOCK_SIZE 16
#define MIN_VIDEO_BITRATE 64000
#define MIN_BUFFER_SIZE 256000
#define MIN_MUX_OVERHEAD 24000
#define DEFAULT_AUDIO_BITRATE 128000
#define LOG_TAG "TsWatermark"
#define ERROR_BUFFER_SIZE 2048

typedef struct SegmentMediaInfo {
    int64_t duration_ms;
    int total_bitrate;
    int video_bitrate;
    int audio_bitrate;
    int has_audio;
} SegmentMediaInfo;

typedef struct WatermarkEncodingProfile {
    int bitrate_controlled;
    int video_bitrate;
    int max_video_bitrate;
    int video_buffer_size;
    int mux_rate;
} WatermarkEncodingProfile;

int run(int argc, char **argv);

static char g_last_ffmpeg_log[8192];
static size_t g_last_ffmpeg_log_length = 0;

static int64_t parse_file_size(const char *path);

static void log_info(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    __android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, format, args);
    va_end(args);
}

static void log_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    __android_log_vprint(ANDROID_LOG_ERROR, LOG_TAG, format, args);
    va_end(args);
}

static void reset_ffmpeg_log_buffer(void)
{
    g_last_ffmpeg_log[0] = '\0';
    g_last_ffmpeg_log_length = 0;
}

static void append_ffmpeg_log_line(const char *message)
{
    size_t message_length;
    size_t writable;

    if (message == NULL || message[0] == '\0') {
        return;
    }

    message_length = strlen(message);
    if (message_length >= sizeof(g_last_ffmpeg_log)) {
        message += message_length - (sizeof(g_last_ffmpeg_log) - 1);
        message_length = sizeof(g_last_ffmpeg_log) - 1;
    }

    if (g_last_ffmpeg_log_length + message_length >= sizeof(g_last_ffmpeg_log)) {
        size_t keep = sizeof(g_last_ffmpeg_log) - 1 - message_length;
        memmove(g_last_ffmpeg_log, g_last_ffmpeg_log + g_last_ffmpeg_log_length - keep, keep);
        g_last_ffmpeg_log_length = keep;
        g_last_ffmpeg_log[g_last_ffmpeg_log_length] = '\0';
    }

    writable = sizeof(g_last_ffmpeg_log) - 1 - g_last_ffmpeg_log_length;
    if (message_length > writable) {
        message_length = writable;
    }

    memcpy(g_last_ffmpeg_log + g_last_ffmpeg_log_length, message, message_length);
    g_last_ffmpeg_log_length += message_length;
    g_last_ffmpeg_log[g_last_ffmpeg_log_length] = '\0';
}

static void ffmpeg_android_log_callback(void *ptr, int level, const char *fmt, va_list vl)
{
    char line[1024];
    int priority = ANDROID_LOG_INFO;

    (void) ptr;

    vsnprintf(line, sizeof(line), fmt, vl);

    if (level <= AV_LOG_ERROR) {
        priority = ANDROID_LOG_ERROR;
    } else if (level <= AV_LOG_WARNING) {
        priority = ANDROID_LOG_WARN;
    } else if (level <= AV_LOG_INFO) {
        priority = ANDROID_LOG_INFO;
    } else {
        priority = ANDROID_LOG_DEBUG;
    }

    __android_log_write(priority, LOG_TAG, line);
    if (level <= AV_LOG_WARNING) {
        append_ffmpeg_log_line(line);
    }
}

static void append_debug_hint(char *error, size_t error_size, const char *prefix)
{
    size_t current_length;
    size_t reserved;
    size_t tail_length;

    if (error == NULL || error_size == 0 || g_last_ffmpeg_log_length == 0) {
        return;
    }

    if (error[0] == '\0') {
        snprintf(error, error_size, "%s%s", prefix, g_last_ffmpeg_log);
        return;
    }

    current_length = strlen(error);
    if (current_length >= error_size - 1) {
        return;
    }

    av_strlcat(error, prefix, error_size);
    current_length = strlen(error);
    if (current_length >= error_size - 1) {
        return;
    }

    reserved = error_size - current_length - 1;
    if (g_last_ffmpeg_log_length <= reserved) {
        av_strlcat(error, g_last_ffmpeg_log, error_size);
        return;
    }

    tail_length = reserved;
    av_strlcat(error, g_last_ffmpeg_log + g_last_ffmpeg_log_length - tail_length, error_size);
}

static void log_file_size(const char *label, const char *path)
{
    int64_t size = parse_file_size(path);
    if (size > 0) {
        log_info("%s大小: %lld 字节, 路径: %s", label, (long long) size, path);
    } else {
        log_info("%s大小暂时无法获取, 路径: %s", label, path ? path : "(null)");
    }
}

static void set_error(char *buffer, size_t buffer_size, const char *message)
{
    if (!buffer || buffer_size == 0) {
        return;
    }
    av_strlcpy(buffer, message, buffer_size);
}

static void set_errorf(char *buffer, size_t buffer_size, const char *prefix, const char *value)
{
    if (!buffer || buffer_size == 0) {
        return;
    }
    snprintf(buffer, buffer_size, "%s%s", prefix, value ? value : "(null)");
}

static void throw_exception(JNIEnv *env, const char *class_name, const char *message)
{
    jclass clazz = (*env)->FindClass(env, class_name);
    if (clazz != NULL) {
        (*env)->ThrowNew(env, clazz, message);
    }
}

static char *dup_jstring(JNIEnv *env, jstring value)
{
    const char *chars;
    char *copy;

    if (value == NULL) {
        return NULL;
    }

    chars = (*env)->GetStringUTFChars(env, value, NULL);
    if (chars == NULL) {
        return NULL;
    }

    copy = av_strdup(chars);
    (*env)->ReleaseStringUTFChars(env, value, chars);
    return copy;
}

static int is_hex_string(const char *value)
{
    size_t i;
    size_t length = strlen(value);
    if ((length & 1) != 0) {
        return 0;
    }
    for (i = 0; i < length; ++i) {
        char c = value[i];
        int is_digit = c >= '0' && c <= '9';
        int is_lower = c >= 'a' && c <= 'f';
        int is_upper = c >= 'A' && c <= 'F';
        if (!is_digit && !is_lower && !is_upper) {
            return 0;
        }
    }
    return 1;
}

static int decode_hex_string(const char *value, uint8_t output[AES_BLOCK_SIZE])
{
    int i;
    for (i = 0; i < AES_BLOCK_SIZE; ++i) {
        int hi = av_tolower(value[i * 2]);
        int lo = av_tolower(value[i * 2 + 1]);
        hi = hi >= 'a' ? hi - 'a' + 10 : hi - '0';
        lo = lo >= 'a' ? lo - 'a' + 10 : lo - '0';
        output[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

static int normalize_aes_bytes(const char *value, const char *label,
                               uint8_t output[AES_BLOCK_SIZE], char *error, size_t error_size)
{
    const char *trimmed_start;
    size_t trimmed_length;
    char *normalized_value = NULL;
    size_t length;
    int result = -1;

    if (value == NULL) {
        set_errorf(error, error_size, label, " is null");
        return -1;
    }

    trimmed_start = value;
    while (*trimmed_start == ' ' || *trimmed_start == '\n' || *trimmed_start == '\r' || *trimmed_start == '\t') {
        trimmed_start++;
    }
    trimmed_length = strlen(trimmed_start);
    while (trimmed_length > 0 && (trimmed_start[trimmed_length - 1] == ' ' ||
            trimmed_start[trimmed_length - 1] == '\n' || trimmed_start[trimmed_length - 1] == '\r' ||
            trimmed_start[trimmed_length - 1] == '\t')) {
        trimmed_length--;
    }
    if (trimmed_length == 0) {
        set_errorf(error, error_size, label, " is empty");
        return -1;
    }

    normalized_value = av_malloc(trimmed_length + 1);
    if (normalized_value == NULL) {
        set_error(error, error_size, "out of memory");
        return -1;
    }
    memcpy(normalized_value, trimmed_start, trimmed_length);
    normalized_value[trimmed_length] = '\0';

    length = trimmed_length;
    if (length == AES_BLOCK_SIZE * 2 && is_hex_string(normalized_value)) {
        result = decode_hex_string(normalized_value, output);
        goto cleanup;
    }

    if (length == AES_BLOCK_SIZE) {
        memcpy(output, normalized_value, AES_BLOCK_SIZE);
        result = 0;
        goto cleanup;
    }

    snprintf(error, error_size, "%s must be 16 raw bytes or 32 hex chars", label);

cleanup:
    av_free(normalized_value);
    return result;
}

static int read_file(const char *path, uint8_t **data, size_t *size, char *error, size_t error_size)
{
    FILE *file;
    long file_size;
    uint8_t *buffer;
    size_t read_size;

    file = fopen(path, "rb");
    if (file == NULL) {
        snprintf(error, error_size, "打开文件失败: %s, 原因: %s", path, strerror(errno));
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        snprintf(error, error_size, "定位文件失败: %s", path);
        return -1;
    }

    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        snprintf(error, error_size, "读取文件大小失败: %s", path);
        return -1;
    }
    rewind(file);

    buffer = av_malloc((size_t)file_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (buffer == NULL) {
        fclose(file);
        set_error(error, error_size, "out of memory");
        return -1;
    }

    read_size = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);
    if (read_size != (size_t)file_size) {
        av_free(buffer);
        snprintf(error, error_size, "读取文件内容失败: %s", path);
        return -1;
    }

    memset(buffer + read_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    *data = buffer;
    *size = read_size;
    return 0;
}

static int write_file(const char *path, const uint8_t *data, size_t size, char *error, size_t error_size)
{
    FILE *file = fopen(path, "wb");
    size_t written;
    if (file == NULL) {
        snprintf(error, error_size, "打开输出文件失败: %s, 原因: %s", path, strerror(errno));
        return -1;
    }
    written = fwrite(data, 1, size, file);
    fclose(file);
    if (written != size) {
        snprintf(error, error_size, "写入文件失败: %s", path);
        return -1;
    }
    return 0;
}

static int decrypt_file_aes128(const char *input_path, const char *output_path,
                               const uint8_t key[AES_BLOCK_SIZE], const uint8_t iv[AES_BLOCK_SIZE],
                               char *error, size_t error_size)
{
    struct AVAES *aes = NULL;
    uint8_t *encrypted = NULL;
    uint8_t *plain = NULL;
    uint8_t iv_copy[AES_BLOCK_SIZE];
    size_t encrypted_size = 0;
    size_t plain_size;
    uint8_t padding;
    int result = -1;

    if (read_file(input_path, &encrypted, &encrypted_size, error, error_size) != 0) {
        goto cleanup;
    }
    if (encrypted_size == 0 || (encrypted_size % AES_BLOCK_SIZE) != 0) {
        set_error(error, error_size, "加密 ts 文件长度非法，必须是 16 的非零整数倍");
        goto cleanup;
    }

    aes = av_aes_alloc();
    if (aes == NULL) {
        set_error(error, error_size, "分配 AES 解密上下文失败");
        goto cleanup;
    }
    if (av_aes_init(aes, key, 128, 1) < 0) {
        set_error(error, error_size, "初始化 AES 解密上下文失败");
        goto cleanup;
    }

    plain = av_malloc(encrypted_size);
    if (plain == NULL) {
        set_error(error, error_size, "out of memory");
        goto cleanup;
    }

    memcpy(iv_copy, iv, AES_BLOCK_SIZE);
    av_aes_crypt(aes, plain, encrypted, (int)(encrypted_size / AES_BLOCK_SIZE), iv_copy, 1);

    padding = plain[encrypted_size - 1];
    if (padding == 0 || padding > AES_BLOCK_SIZE) {
        set_error(error, error_size, "解密后 PKCS7 padding 非法");
        goto cleanup;
    }
    for (size_t i = 0; i < padding; ++i) {
        if (plain[encrypted_size - 1 - i] != padding) {
            set_error(error, error_size, "解密后 PKCS7 padding 校验失败");
            goto cleanup;
        }
    }

    plain_size = encrypted_size - padding;
    if (write_file(output_path, plain, plain_size, error, error_size) != 0) {
        goto cleanup;
    }

    result = 0;

cleanup:
    av_free(aes);
    av_free(encrypted);
    av_free(plain);
    return result;
}

static int encrypt_file_aes128(const char *input_path, const char *output_path,
                               const uint8_t key[AES_BLOCK_SIZE], const uint8_t iv[AES_BLOCK_SIZE],
                               char *error, size_t error_size)
{
    struct AVAES *aes = NULL;
    uint8_t *plain = NULL;
    uint8_t *padded = NULL;
    uint8_t *encrypted = NULL;
    uint8_t iv_copy[AES_BLOCK_SIZE];
    size_t plain_size = 0;
    size_t padded_size;
    uint8_t padding;
    int result = -1;

    if (read_file(input_path, &plain, &plain_size, error, error_size) != 0) {
        goto cleanup;
    }

    padding = (uint8_t)(AES_BLOCK_SIZE - (plain_size % AES_BLOCK_SIZE));
    if (padding == 0) {
        padding = AES_BLOCK_SIZE;
    }
    padded_size = plain_size + padding;

    padded = av_malloc(padded_size);
    encrypted = av_malloc(padded_size);
    if (padded == NULL || encrypted == NULL) {
        set_error(error, error_size, "内存不足");
        goto cleanup;
    }

    memcpy(padded, plain, plain_size);
    memset(padded + plain_size, padding, padding);

    aes = av_aes_alloc();
    if (aes == NULL) {
        set_error(error, error_size, "分配 AES 加密上下文失败");
        goto cleanup;
    }
    if (av_aes_init(aes, key, 128, 0) < 0) {
        set_error(error, error_size, "初始化 AES 加密上下文失败");
        goto cleanup;
    }

    memcpy(iv_copy, iv, AES_BLOCK_SIZE);
    av_aes_crypt(aes, encrypted, padded, (int)(padded_size / AES_BLOCK_SIZE), iv_copy, 0);

    if (write_file(output_path, encrypted, padded_size, error, error_size) != 0) {
        goto cleanup;
    }

    result = 0;

cleanup:
    av_free(aes);
    av_free(plain);
    av_free(padded);
    av_free(encrypted);
    return result;
}

static int64_t parse_file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return st.st_size;
}

static int clamp_positive_int(int64_t value)
{
    if (value <= 0) {
        return 0;
    }
    if (value > INT32_MAX) {
        return INT32_MAX;
    }
    return (int)value;
}

static int estimate_total_bitrate(int64_t file_size_bytes, int64_t duration_ms)
{
    if (file_size_bytes <= 0 || duration_ms <= 0) {
        return 0;
    }
    return clamp_positive_int((file_size_bytes * 8000LL) / duration_ms);
}

static int read_segment_media_info(const char *input_path, SegmentMediaInfo *media_info,
                                   char *error, size_t error_size)
{
    AVFormatContext *format_context = NULL;
    int result;

    memset(media_info, 0, sizeof(*media_info));

    result = avformat_open_input(&format_context, input_path, NULL, NULL);
    if (result < 0) {
        char ff_error[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(result, ff_error, sizeof(ff_error));
        snprintf(error, error_size, "打开解密后的 ts 失败: %s", ff_error);
        append_debug_hint(error, error_size, "；FFmpeg日志: ");
        return -1;
    }

    result = avformat_find_stream_info(format_context, NULL);
    if (result < 0) {
        char ff_error[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(result, ff_error, sizeof(ff_error));
        snprintf(error, error_size, "探测 ts 音视频信息失败: %s", ff_error);
        append_debug_hint(error, error_size, "；FFmpeg日志: ");
        avformat_close_input(&format_context);
        return -1;
    }

    if (format_context->duration > 0) {
        media_info->duration_ms = format_context->duration / (AV_TIME_BASE / 1000);
    }
    media_info->total_bitrate = clamp_positive_int(format_context->bit_rate);

    for (unsigned int i = 0; i < format_context->nb_streams; ++i) {
        AVStream *stream = format_context->streams[i];
        AVCodecParameters *codecpar = stream->codecpar;
        int stream_bitrate = clamp_positive_int(codecpar->bit_rate);

        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (stream_bitrate > media_info->video_bitrate) {
                media_info->video_bitrate = stream_bitrate;
            }
        } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            media_info->has_audio = 1;
            media_info->audio_bitrate += stream_bitrate;
        }
    }

    avformat_close_input(&format_context);

    if (media_info->total_bitrate <= 0 && media_info->duration_ms > 0) {
        media_info->total_bitrate = estimate_total_bitrate(parse_file_size(input_path), media_info->duration_ms);
    }

    return 0;
}

static int scale_bitrate(int bitrate, double ratio)
{
    return (int)((bitrate * ratio) + 0.5);
}

static int estimate_mux_overhead(int bitrate)
{
    int scaled = scale_bitrate(bitrate, 0.03);
    return scaled > MIN_MUX_OVERHEAD ? scaled : MIN_MUX_OVERHEAD;
}

static WatermarkEncodingProfile create_profile(const SegmentMediaInfo *media_info)
{
    WatermarkEncodingProfile profile;
    int reserved_audio_bitrate = 0;
    int target_video_bitrate;
    int mux_rate_base;

    memset(&profile, 0, sizeof(profile));

    if (media_info->has_audio) {
        reserved_audio_bitrate = media_info->audio_bitrate > 0
                ? media_info->audio_bitrate
                : DEFAULT_AUDIO_BITRATE;
    }

    if (media_info->video_bitrate > 0) {
        target_video_bitrate = media_info->video_bitrate;
    } else if (media_info->total_bitrate > 0) {
        target_video_bitrate = media_info->total_bitrate - reserved_audio_bitrate
                - estimate_mux_overhead(media_info->total_bitrate);
        if (target_video_bitrate < MIN_VIDEO_BITRATE) {
            target_video_bitrate = MIN_VIDEO_BITRATE;
        }
    } else {
        return profile;
    }

    profile.bitrate_controlled = 1;
    profile.video_bitrate = target_video_bitrate;
    profile.max_video_bitrate = FFMAX(target_video_bitrate, scale_bitrate(target_video_bitrate, 1.10));
    profile.video_buffer_size = FFMAX(MIN_BUFFER_SIZE, target_video_bitrate * 2);
    mux_rate_base = media_info->total_bitrate > 0
            ? media_info->total_bitrate
            : target_video_bitrate + reserved_audio_bitrate
                    + estimate_mux_overhead(target_video_bitrate + reserved_audio_bitrate);
    profile.mux_rate = FFMAX(mux_rate_base,
            target_video_bitrate + reserved_audio_bitrate + estimate_mux_overhead(mux_rate_base));
    return profile;
}

static char *dup_printf_int(int value)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return av_strdup(buffer);
}

static void log_ffmpeg_command(int argc, char **argv)
{
    char command[2048];
    size_t used = 0;

    command[0] = '\0';
    for (int i = 0; i < argc; ++i) {
        const char *arg = argv[i] ? argv[i] : "(null)";
        int written;

        if (used >= sizeof(command) - 1) {
            break;
        }
        written = snprintf(command + used, sizeof(command) - used, "%s%s",
                i == 0 ? "" : " ", arg);
        if (written < 0) {
            break;
        }
        if ((size_t) written >= sizeof(command) - used) {
            used = sizeof(command) - 1;
            break;
        }
        used += (size_t) written;
    }

    if (used >= sizeof(command) - 1) {
        av_strlcat(command, " ...", sizeof(command));
    }
    log_info("本次 FFmpeg 命令: %s", command);
}

static void free_args(char **argv, int argc)
{
    if (argv == NULL) {
        return;
    }
    for (int i = 0; i < argc; ++i) {
        av_free(argv[i]);
    }
    av_free(argv);
}

static int build_ffmpeg_args(const char *input_path, const char *watermark_path, const char *output_path,
                             const WatermarkEncodingProfile *profile,
                             int *argc_out, char ***argv_out, char *error, size_t error_size)
{
    int argc = profile->bitrate_controlled ? 35 : 29;
    char **argv = av_calloc((size_t)argc, sizeof(char *));
    int i = 0;

    if (argv == NULL) {
        set_error(error, error_size, "内存不足");
        return -1;
    }

    argv[i++] = av_strdup("ffmpeg");
    argv[i++] = av_strdup("-hide_banner");
    argv[i++] = av_strdup("-loglevel");
    argv[i++] = av_strdup("info");
    argv[i++] = av_strdup("-i");
    argv[i++] = av_strdup(input_path);
    argv[i++] = av_strdup("-i");
    argv[i++] = av_strdup(watermark_path);
    argv[i++] = av_strdup("-filter_complex");
    argv[i++] = av_strdup("[0:v][1:v]overlay=10:10[vout]");
    argv[i++] = av_strdup("-map");
    argv[i++] = av_strdup("[vout]");
    argv[i++] = av_strdup("-map");
    argv[i++] = av_strdup("0:a?");
    argv[i++] = av_strdup("-c:v");
    argv[i++] = av_strdup("libx264");
    argv[i++] = av_strdup("-preset");
    argv[i++] = av_strdup("veryfast");
    argv[i++] = av_strdup("-pix_fmt");
    argv[i++] = av_strdup("yuv420p");
    argv[i++] = av_strdup("-c:a");
    argv[i++] = av_strdup("copy");

    if (profile->bitrate_controlled) {
        argv[i++] = av_strdup("-b:v");
        argv[i++] = dup_printf_int(profile->video_bitrate);
        argv[i++] = av_strdup("-maxrate");
        argv[i++] = dup_printf_int(profile->max_video_bitrate);
        argv[i++] = av_strdup("-bufsize");
        argv[i++] = dup_printf_int(profile->video_buffer_size);
        argv[i++] = av_strdup("-muxrate");
        argv[i++] = dup_printf_int(profile->mux_rate);
    } else {
        argv[i++] = av_strdup("-crf");
        argv[i++] = av_strdup("23");
    }

    argv[i++] = av_strdup("-f");
    argv[i++] = av_strdup("mpegts");
    argv[i++] = av_strdup("-y");
    argv[i++] = av_strdup(output_path);

    for (int j = 0; j < argc; ++j) {
        if (argv[j] == NULL) {
            free_args(argv, argc);
            set_error(error, error_size, "构建 FFmpeg 参数失败，内存不足");
            return -1;
        }
    }

    *argc_out = argc;
    *argv_out = argv;
    return 0;
}

static int ensure_directory_recursive(const char *directory, char *error, size_t error_size)
{
    char *path;
    size_t length;

    if (directory == NULL || directory[0] == '\0') {
        set_error(error, error_size, "输出文件必须有父目录");
        return -1;
    }

    path = av_strdup(directory);
    if (path == NULL) {
        set_error(error, error_size, "内存不足");
        return -1;
    }

    length = strlen(path);
    if (length > 1 && path[length - 1] == '/') {
        path[length - 1] = '\0';
    }

    for (char *cursor = path + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (mkdir(path, 0777) != 0 && errno != EEXIST) {
                snprintf(error, error_size, "创建输出目录失败: %s", path);
                av_free(path);
                return -1;
            }
            *cursor = '/';
        }
    }

    if (mkdir(path, 0777) != 0 && errno != EEXIST) {
        snprintf(error, error_size, "创建输出目录失败: %s", path);
        av_free(path);
        return -1;
    }

    av_free(path);
    return 0;
}

static char *parent_directory_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    char *parent;
    if (slash == NULL || slash == path) {
        return slash == path ? av_strdup("/") : NULL;
    }
    parent = av_malloc((size_t)(slash - path) + 1);
    if (parent == NULL) {
        return NULL;
    }
    memcpy(parent, path, (size_t)(slash - path));
    parent[slash - path] = '\0';
    return parent;
}

static int create_workspace(const char *parent_dir, char **workspace_out, char *error, size_t error_size)
{
    char *pattern = av_asprintf("%s/.ts-watermark-XXXXXX", parent_dir);
    if (pattern == NULL) {
        set_error(error, error_size, "内存不足");
        return -1;
    }
    if (mkdtemp(pattern) == NULL) {
        snprintf(error, error_size, "创建临时工作目录失败: %s", strerror(errno));
        av_free(pattern);
        return -1;
    }
    *workspace_out = pattern;
    return 0;
}

static void cleanup_workspace(const char *workspace)
{
    char *plain_input;
    char *plain_output;

    if (workspace == NULL) {
        return;
    }

    plain_input = av_asprintf("%s/input_plain.ts", workspace);
    plain_output = av_asprintf("%s/output_plain.ts", workspace);
    if (plain_input != NULL) {
        unlink(plain_input);
    }
    if (plain_output != NULL) {
        unlink(plain_output);
    }
    rmdir(workspace);
    av_free(plain_input);
    av_free(plain_output);
}

static int watermark_segment_native(const char *encrypted_ts_path, const char *watermark_path,
                                    const char *output_path, const char *key_input, const char *iv_input,
                                    char *error, size_t error_size)
{
    uint8_t key[AES_BLOCK_SIZE];
    uint8_t iv[AES_BLOCK_SIZE];
    char *parent_dir = NULL;
    char *workspace = NULL;
    char *plain_input = NULL;
    char *plain_output = NULL;
    SegmentMediaInfo media_info;
    WatermarkEncodingProfile profile;
    char **argv = NULL;
    int argc = 0;
    int ffmpeg_result;
    int result = -1;
    struct stat st;

    log_info("开始处理加密 TS，输入: %s", encrypted_ts_path ? encrypted_ts_path : "(null)");
    reset_ffmpeg_log_buffer();
    av_log_set_level(AV_LOG_INFO);
    av_log_set_callback(ffmpeg_android_log_callback);

    if (encrypted_ts_path == NULL || stat(encrypted_ts_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        snprintf(error, error_size, "加密 ts 文件不存在: %s", encrypted_ts_path);
        goto cleanup;
    }
    if (watermark_path == NULL || stat(watermark_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        snprintf(error, error_size, "水印文件不存在: %s", watermark_path);
        goto cleanup;
    }
    if (output_path == NULL || output_path[0] == '\0') {
        set_error(error, error_size, "输出路径不能为空");
        goto cleanup;
    }

    if (normalize_aes_bytes(key_input, "key", key, error, error_size) != 0 ||
            normalize_aes_bytes(iv_input, "iv", iv, error, error_size) != 0) {
        goto cleanup;
    }

    log_file_size("输入加密TS", encrypted_ts_path);
    log_file_size("输入水印图", watermark_path);

    parent_dir = parent_directory_of(output_path);
    if (parent_dir == NULL) {
        set_error(error, error_size, "输出文件必须有父目录");
        goto cleanup;
    }
    if (ensure_directory_recursive(parent_dir, error, error_size) != 0) {
        goto cleanup;
    }
    if (create_workspace(parent_dir, &workspace, error, error_size) != 0) {
        goto cleanup;
    }

    plain_input = av_asprintf("%s/input_plain.ts", workspace);
    plain_output = av_asprintf("%s/output_plain.ts", workspace);
    if (plain_input == NULL || plain_output == NULL) {
        set_error(error, error_size, "内存不足");
        goto cleanup;
    }

    log_info("步骤1/4：开始解密 TS");
    if (decrypt_file_aes128(encrypted_ts_path, plain_input, key, iv, error, error_size) != 0) {
        log_error("步骤1/4失败：%s", error);
        goto cleanup;
    }
    log_info("步骤1/4完成：TS 解密成功，输出: %s", plain_input);
    log_file_size("解密后明文TS", plain_input);

    log_info("步骤2/4：读取 TS 媒体信息");
    if (read_segment_media_info(plain_input, &media_info, error, error_size) != 0) {
        log_error("步骤2/4失败：%s", error);
        goto cleanup;
    }
    log_info("步骤2/4完成：durationMs=%lld, totalBitrate=%d, videoBitrate=%d, audioBitrate=%d, hasAudio=%d",
            (long long) media_info.duration_ms, media_info.total_bitrate, media_info.video_bitrate,
            media_info.audio_bitrate, media_info.has_audio);
    profile = create_profile(&media_info);
    log_info("目标编码参数：bitrateControlled=%d, videoBitrate=%d, maxVideoBitrate=%d, bufferSize=%d, muxRate=%d",
            profile.bitrate_controlled, profile.video_bitrate, profile.max_video_bitrate,
            profile.video_buffer_size, profile.mux_rate);

    if (build_ffmpeg_args(plain_input, watermark_path, plain_output, &profile,
            &argc, &argv, error, error_size) != 0) {
        log_error("构建 FFmpeg 参数失败：%s", error);
        goto cleanup;
    }
    log_ffmpeg_command(argc, argv);

    log_info("步骤3/4：开始执行 FFmpeg 叠加水印");
    reset_ffmpeg_log_buffer();
    av_log_set_level(AV_LOG_INFO);
    av_log_set_callback(ffmpeg_android_log_callback);
    ffmpeg_result = run(argc, argv);
    if (ffmpeg_result != 0) {
        snprintf(error, error_size, "FFmpeg 叠加水印失败，退出码: %d", ffmpeg_result);
        append_debug_hint(error, error_size, "；最近日志: ");
        log_error("步骤3/4失败：%s", error);
        goto cleanup;
    }
    log_info("步骤3/4完成：水印叠加成功，输出: %s", plain_output);
    log_file_size("加水印后明文TS", plain_output);

    log_info("步骤4/4：开始重新加密 TS");
    if (encrypt_file_aes128(plain_output, output_path, key, iv, error, error_size) != 0) {
        log_error("步骤4/4失败：%s", error);
        goto cleanup;
    }
    log_info("步骤4/4完成：重新加密成功，输出: %s", output_path);
    log_file_size("最终加密TS", output_path);

    result = 0;

cleanup:
    if (result != 0 && error != NULL && error[0] == '\0') {
        set_error(error, error_size, "处理失败，但未拿到明确错误信息，请查看 logcat 中的 TsWatermark 日志");
    }
    free_args(argv, argc);
    cleanup_workspace(workspace);
    av_free(parent_dir);
    av_free(workspace);
    av_free(plain_input);
    av_free(plain_output);
    return result;
}

JNIEXPORT void JNICALL
Java_com_dongao_lib_tswatermark_EncryptedTsWatermarker_nativeWatermarkSegment(
        JNIEnv *env, jclass clazz, jstring encrypted_ts_path, jstring watermark_path,
        jstring output_path, jstring key, jstring iv)
{
    char error[ERROR_BUFFER_SIZE];
    char *encrypted_ts = NULL;
    char *watermark = NULL;
    char *output = NULL;
    char *key_value = NULL;
    char *iv_value = NULL;

    (void) clazz;
    error[0] = '\0';

    encrypted_ts = dup_jstring(env, encrypted_ts_path);
    watermark = dup_jstring(env, watermark_path);
    output = dup_jstring(env, output_path);
    key_value = dup_jstring(env, key);
    iv_value = dup_jstring(env, iv);

    if (encrypted_ts == NULL || watermark == NULL || output == NULL || key_value == NULL || iv_value == NULL) {
        if (!(*env)->ExceptionCheck(env)) {
            throw_exception(env, "java/lang/OutOfMemoryError", "分配 JNI 字符串失败");
        }
        goto cleanup;
    }

    if (watermark_segment_native(encrypted_ts, watermark, output, key_value, iv_value,
            error, sizeof(error)) != 0) {
        log_error("nativeWatermarkSegment 失败：%s", error);
        throw_exception(env, "java/lang/IllegalStateException", error);
    }

cleanup:
    av_free(encrypted_ts);
    av_free(watermark);
    av_free(output);
    av_free(key_value);
    av_free(iv_value);
}
