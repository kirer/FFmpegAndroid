#include <jni.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
        snprintf(error, error_size, "failed to open %s: %s", path, strerror(errno));
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        snprintf(error, error_size, "failed to seek %s", path);
        return -1;
    }

    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        snprintf(error, error_size, "failed to read size for %s", path);
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
        snprintf(error, error_size, "failed to read %s", path);
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
        snprintf(error, error_size, "failed to open %s: %s", path, strerror(errno));
        return -1;
    }
    written = fwrite(data, 1, size, file);
    fclose(file);
    if (written != size) {
        snprintf(error, error_size, "failed to write %s", path);
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
        set_error(error, error_size, "encrypted ts length must be a non-zero multiple of 16");
        goto cleanup;
    }

    aes = av_aes_alloc();
    if (aes == NULL) {
        set_error(error, error_size, "failed to allocate AES context");
        goto cleanup;
    }
    if (av_aes_init(aes, key, 128, 1) < 0) {
        set_error(error, error_size, "failed to initialize AES decrypt context");
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
        set_error(error, error_size, "invalid PKCS7 padding");
        goto cleanup;
    }
    for (size_t i = 0; i < padding; ++i) {
        if (plain[encrypted_size - 1 - i] != padding) {
            set_error(error, error_size, "invalid PKCS7 padding");
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
        set_error(error, error_size, "out of memory");
        goto cleanup;
    }

    memcpy(padded, plain, plain_size);
    memset(padded + plain_size, padding, padding);

    aes = av_aes_alloc();
    if (aes == NULL) {
        set_error(error, error_size, "failed to allocate AES context");
        goto cleanup;
    }
    if (av_aes_init(aes, key, 128, 0) < 0) {
        set_error(error, error_size, "failed to initialize AES encrypt context");
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
        snprintf(error, error_size, "failed to open decrypted ts: %s", ff_error);
        return -1;
    }

    result = avformat_find_stream_info(format_context, NULL);
    if (result < 0) {
        char ff_error[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(result, ff_error, sizeof(ff_error));
        snprintf(error, error_size, "failed to probe ts stream info: %s", ff_error);
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
    int argc = profile->bitrate_controlled ? 31 : 25;
    char **argv = av_calloc((size_t)argc, sizeof(char *));
    int i = 0;

    if (argv == NULL) {
        set_error(error, error_size, "out of memory");
        return -1;
    }

    argv[i++] = av_strdup("ffmpeg");
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
            set_error(error, error_size, "out of memory");
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
        set_error(error, error_size, "output file must have a parent directory");
        return -1;
    }

    path = av_strdup(directory);
    if (path == NULL) {
        set_error(error, error_size, "out of memory");
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
                snprintf(error, error_size, "failed to create output directory: %s", path);
                av_free(path);
                return -1;
            }
            *cursor = '/';
        }
    }

    if (mkdir(path, 0777) != 0 && errno != EEXIST) {
        snprintf(error, error_size, "failed to create output directory: %s", path);
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
        set_error(error, error_size, "out of memory");
        return -1;
    }
    if (mkdtemp(pattern) == NULL) {
        snprintf(error, error_size, "failed to create temp workspace: %s", strerror(errno));
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

    if (encrypted_ts_path == NULL || stat(encrypted_ts_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        snprintf(error, error_size, "encrypted ts file does not exist: %s", encrypted_ts_path);
        goto cleanup;
    }
    if (watermark_path == NULL || stat(watermark_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        snprintf(error, error_size, "watermark file does not exist: %s", watermark_path);
        goto cleanup;
    }

    if (normalize_aes_bytes(key_input, "key", key, error, error_size) != 0 ||
            normalize_aes_bytes(iv_input, "iv", iv, error, error_size) != 0) {
        goto cleanup;
    }

    parent_dir = parent_directory_of(output_path);
    if (parent_dir == NULL) {
        set_error(error, error_size, "output file must have a parent directory");
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
        set_error(error, error_size, "out of memory");
        goto cleanup;
    }

    if (decrypt_file_aes128(encrypted_ts_path, plain_input, key, iv, error, error_size) != 0) {
        goto cleanup;
    }
    if (read_segment_media_info(plain_input, &media_info, error, error_size) != 0) {
        goto cleanup;
    }
    profile = create_profile(&media_info);

    if (build_ffmpeg_args(plain_input, watermark_path, plain_output, &profile,
            &argc, &argv, error, error_size) != 0) {
        goto cleanup;
    }

    ffmpeg_result = run(argc, argv);
    if (ffmpeg_result != 0) {
        snprintf(error, error_size, "ffmpeg watermark failed with exit code %d", ffmpeg_result);
        goto cleanup;
    }

    if (encrypt_file_aes128(plain_output, output_path, key, iv, error, error_size) != 0) {
        goto cleanup;
    }

    result = 0;

cleanup:
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
    char error[256];
    char *encrypted_ts = NULL;
    char *watermark = NULL;
    char *output = NULL;
    char *key_value = NULL;
    char *iv_value = NULL;

    (void) clazz;

    encrypted_ts = dup_jstring(env, encrypted_ts_path);
    watermark = dup_jstring(env, watermark_path);
    output = dup_jstring(env, output_path);
    key_value = dup_jstring(env, key);
    iv_value = dup_jstring(env, iv);

    if (encrypted_ts == NULL || watermark == NULL || output == NULL || key_value == NULL || iv_value == NULL) {
        if (!(*env)->ExceptionCheck(env)) {
            throw_exception(env, "java/lang/OutOfMemoryError", "failed to allocate JNI strings");
        }
        goto cleanup;
    }

    if (watermark_segment_native(encrypted_ts, watermark, output, key_value, iv_value,
            error, sizeof(error)) != 0) {
        throw_exception(env, "java/lang/IllegalStateException", error);
    }

cleanup:
    av_free(encrypted_ts);
    av_free(watermark);
    av_free(output);
    av_free(key_value);
    av_free(iv_value);
}
