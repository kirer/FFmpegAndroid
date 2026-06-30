#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODULE_DIR="$ROOT_DIR/lib_ts_watermark"
OUTPUT_DIR="$MODULE_DIR/src/main/jniLibs/arm64-v8a"
OUTPUT_SO="$OUTPUT_DIR/libts-watermark.so"
NATIVE_SOURCE="$MODULE_DIR/native/ts_watermark_native.c"

WORK_ROOT="${WORK_ROOT:-${TMPDIR:-/tmp}/tswm-single-build}"
SRC_ROOT="$WORK_ROOT/src"
PREFIX="$WORK_ROOT/prefix"
LINK_ROOT="$WORK_ROOT/link"
BUILD_ROOT="$LINK_ROOT/build"

DEFAULT_FFMPEG_REPO="https://github.com/FFmpeg/FFmpeg.git"
DEFAULT_X264_REPO="https://github.com/mirror/x264.git"
if [[ -d "/tmp/tswm-src/ffmpeg/.git" ]]; then
    DEFAULT_FFMPEG_REPO="/tmp/tswm-src/ffmpeg"
fi
if [[ -d "/tmp/tswm-src/x264/.git" ]]; then
    DEFAULT_X264_REPO="/tmp/tswm-src/x264"
fi

FFMPEG_REPO="${FFMPEG_REPO:-$DEFAULT_FFMPEG_REPO}"
FFMPEG_COMMIT="${FFMPEG_COMMIT:-ea3d24bbe3c58b171e55fe2151fc7ffaca3ab3d2}"
X264_REPO="${X264_REPO:-$DEFAULT_X264_REPO}"
X264_COMMIT="${X264_COMMIT:-b35605ace3ddf7c1a5d67a2eb553f034aef41d55}"
ANDROID_API="${ANDROID_API:-21}"
SKIP_DEPENDENCY_BUILD="${SKIP_DEPENDENCY_BUILD:-0}"

cpu_count() {
    if command -v sysctl >/dev/null 2>&1; then
        sysctl -n hw.ncpu 2>/dev/null || echo 8
    else
        getconf _NPROCESSORS_ONLN 2>/dev/null || echo 8
    fi
}

host_tag() {
    case "$(uname -s)" in
        Darwin) echo "darwin-x86_64" ;;
        Linux) echo "linux-x86_64" ;;
        *)
            echo "Unsupported host OS: $(uname -s)" >&2
            exit 1
            ;;
    esac
}

find_ndk() {
    if [[ -n "${ANDROID_NDK_ROOT:-}" && -d "${ANDROID_NDK_ROOT}" ]]; then
        echo "${ANDROID_NDK_ROOT}"
        return
    fi
    if [[ -n "${ANDROID_NDK_HOME:-}" && -d "${ANDROID_NDK_HOME}" ]]; then
        echo "${ANDROID_NDK_HOME}"
        return
    fi

    local sdk_root="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Library/Android/sdk}}"
    local ndk_root="$sdk_root/ndk"
    if [[ ! -d "$ndk_root" ]]; then
        echo "Android NDK not found. Set ANDROID_NDK_ROOT or install an NDK under $ndk_root." >&2
        exit 1
    fi

    find "$ndk_root" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -n 1
}

clone_or_update() {
    local repo_url="$1"
    local dest_dir="$2"
    local ref="$3"

    if [[ ! -d "$dest_dir/.git" ]]; then
        rm -rf "$dest_dir"
        git clone "$repo_url" "$dest_dir"
    fi

    git -C "$dest_dir" fetch --all --tags --prune
    git -C "$dest_dir" checkout "$ref"
}

generate_link_project() {
    mkdir -p "$LINK_ROOT"

    mkdir -p "$LINK_ROOT/overlay"
    cp "$SRC_ROOT/ffmpeg/fftools/cmdutils.c" "$LINK_ROOT/overlay/cmdutils.c"
    cp "$SRC_ROOT/ffmpeg/fftools/cmdutils.h" "$LINK_ROOT/overlay/cmdutils.h"
    cp "$SRC_ROOT/ffmpeg/fftools/ffmpeg.c" "$LINK_ROOT/overlay/ffmpeg.c"

    python3 - <<'PY' "$LINK_ROOT/overlay/cmdutils.h" "$LINK_ROOT/overlay/cmdutils.c" "$LINK_ROOT/overlay/ffmpeg.c"
import pathlib
import sys

cmdutils_h = pathlib.Path(sys.argv[1])
cmdutils_c = pathlib.Path(sys.argv[2])
ffmpeg_c = pathlib.Path(sys.argv[3])

text = cmdutils_h.read_text()
marker = '#endif\n\n/**\n * program name, defined by the program for show_version().\n */'
replacement = '#endif\n\n#include <setjmp.h>\n\nextern jmp_buf jump_buf;\n\n/**\n * program name, defined by the program for show_version().\n */'
if marker not in text:
    raise SystemExit('Failed to patch cmdutils.h: marker not found')
cmdutils_h.write_text(text.replace(marker, replacement, 1))

text = cmdutils_c.read_text()
old = '    exit(ret);\n'
new = '    longjmp(jump_buf, 1);\n'
if old not in text:
    raise SystemExit('Failed to patch cmdutils.c: exit marker not found')
cmdutils_c.write_text(text.replace(old, new, 1))

text = ffmpeg_c.read_text()
marker = 'const int program_birth_year = 2000;\n'
replacement = 'const int program_birth_year = 2000;\n\n#include <setjmp.h>\n\njmp_buf jump_buf;\n'
if marker not in text:
    raise SystemExit('Failed to patch ffmpeg.c: birth year marker not found')
text = text.replace(marker, replacement, 1)

old = 'int main(int argc, char **argv)\n'
new = 'int run(int argc, char **argv)\n'
if old not in text:
    raise SystemExit('Failed to patch ffmpeg.c: main marker not found')
text = text.replace(old, new, 1)

old = '    int ret;\n    BenchmarkTimeStamps ti;\n'
new = '    int ret;\n    BenchmarkTimeStamps ti;\n    main_return_code = 0;\n'
if old not in text:
    raise SystemExit('Failed to patch ffmpeg.c: ti marker not found')
text = text.replace(old, new, 1)

old = '    av_log_set_flags(AV_LOG_SKIP_REPEATED);\n    parse_loglevel(argc, argv, options);\n\n#if CONFIG_AVDEVICE\n'
new = '    av_log_set_flags(AV_LOG_SKIP_REPEATED);\n    parse_loglevel(argc, argv, options);\n\n    if (setjmp(jump_buf)) {\n        main_return_code = 1;\n        goto end;\n    }\n\n#if CONFIG_AVDEVICE\n'
if old not in text:
    raise SystemExit('Failed to patch ffmpeg.c: setjmp marker not found')
text = text.replace(old, new, 1)

old = '    exit_program(received_nb_signals ? 255 : main_return_code);\n    return main_return_code;\n}\n'
new = 'end:\n    return main_return_code;\n}\n'
if old not in text:
    raise SystemExit('Failed to patch ffmpeg.c: final return marker not found')
text = text.replace(old, new, 1)

ffmpeg_c.write_text(text)
PY

    cat >"$LINK_ROOT/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.10)
project(tswm_single C)

set(FFMPEG_SRC "$SRC_ROOT/ffmpeg")
set(OVERLAY_SRC "$LINK_ROOT/overlay")
set(PREFIX "$PREFIX")
set(NATIVE_SRC "$NATIVE_SOURCE")

add_library(ts-watermark SHARED
    \${NATIVE_SRC}
    \${OVERLAY_SRC}/cmdutils.c
    \${OVERLAY_SRC}/ffmpeg.c
    \${FFMPEG_SRC}/fftools/ffmpeg_demux.c
    \${FFMPEG_SRC}/fftools/ffmpeg_filter.c
    \${FFMPEG_SRC}/fftools/ffmpeg_hw.c
    \${FFMPEG_SRC}/fftools/ffmpeg_mux.c
    \${FFMPEG_SRC}/fftools/ffmpeg_mux_init.c
    \${FFMPEG_SRC}/fftools/ffmpeg_opt.c
    \${FFMPEG_SRC}/fftools/objpool.c
    \${FFMPEG_SRC}/fftools/opt_common.c
    \${FFMPEG_SRC}/fftools/sync_queue.c
    \${FFMPEG_SRC}/fftools/thread_queue.c)

target_include_directories(ts-watermark PRIVATE
    \${FFMPEG_SRC}
    \${FFMPEG_SRC}/fftools
    \${OVERLAY_SRC}
    \${PREFIX}/include)

add_library(avcodec STATIC IMPORTED)
set_target_properties(avcodec PROPERTIES IMPORTED_LOCATION \${PREFIX}/lib/libavcodec.a)
add_library(avformat STATIC IMPORTED)
set_target_properties(avformat PROPERTIES IMPORTED_LOCATION \${PREFIX}/lib/libavformat.a)
add_library(avfilter STATIC IMPORTED)
set_target_properties(avfilter PROPERTIES IMPORTED_LOCATION \${PREFIX}/lib/libavfilter.a)
add_library(avutil STATIC IMPORTED)
set_target_properties(avutil PROPERTIES IMPORTED_LOCATION \${PREFIX}/lib/libavutil.a)
add_library(swscale STATIC IMPORTED)
set_target_properties(swscale PROPERTIES IMPORTED_LOCATION \${PREFIX}/lib/libswscale.a)
add_library(x264 STATIC IMPORTED)
set_target_properties(x264 PROPERTIES IMPORTED_LOCATION \${PREFIX}/lib/libx264.a)

find_library(log-lib log)
find_library(z-lib z)
find_library(m-lib m)

target_compile_options(ts-watermark PRIVATE
    -Os
    -ffunction-sections
    -fdata-sections
    -fvisibility=hidden
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables)

target_link_options(ts-watermark PRIVATE
    -Wl,--gc-sections
    -Wl,--exclude-libs,ALL
    -Wl,--build-id=none)

target_link_libraries(ts-watermark
    avfilter
    avformat
    avcodec
    swscale
    avutil
    x264
    \${z-lib}
    \${log-lib}
    \${m-lib})
EOF
}

main() {
    local ndk_root
    ndk_root="$(find_ndk)"
    local host
    host="$(host_tag)"

    local toolchain_root="$ndk_root/toolchains/llvm/prebuilt/$host"
    local sysroot="$toolchain_root/sysroot"
    local tool_bin="$toolchain_root/bin"
    local cc="$tool_bin/aarch64-linux-android${ANDROID_API}-clang"
    local cxx="$tool_bin/aarch64-linux-android${ANDROID_API}-clang++"
    local ar="$tool_bin/llvm-ar"
    local nm="$tool_bin/llvm-nm"
    local ranlib="$tool_bin/llvm-ranlib"
    local strip="$tool_bin/llvm-strip"
    local pkg_config_bin="${PKG_CONFIG_BIN:-$(command -v pkg-config)}"
    local jobs
    jobs="$(cpu_count)"

    if [[ ! -f "$NATIVE_SOURCE" ]]; then
        echo "Native source not found: $NATIVE_SOURCE" >&2
        exit 1
    fi

    mkdir -p "$SRC_ROOT" "$OUTPUT_DIR"
    if [[ "$SKIP_DEPENDENCY_BUILD" != "1" ]]; then
        rm -rf "$PREFIX"
        mkdir -p "$PREFIX"

        clone_or_update "$X264_REPO" "$SRC_ROOT/x264" "$X264_COMMIT"
        clone_or_update "$FFMPEG_REPO" "$SRC_ROOT/ffmpeg" "$FFMPEG_COMMIT"

        pushd "$SRC_ROOT/x264" >/dev/null
        make distclean >/dev/null 2>&1 || true
        CC="$cc" \
        AS="$cc" \
        AR="$ar" \
        LD="$cc" \
        RANLIB="$ranlib" \
        STRIP="$strip" \
        CFLAGS="--sysroot=$sysroot -fPIC -Oz -ffunction-sections -fdata-sections -fvisibility=hidden -fomit-frame-pointer -fno-unwind-tables -fno-asynchronous-unwind-tables" \
        LDFLAGS="--sysroot=$sysroot -Wl,--gc-sections -lm" \
        ./configure \
            --prefix="$PREFIX" \
            --host=aarch64-linux \
            --enable-static \
            --disable-cli \
            --disable-opencl \
            --disable-thread \
            --disable-interlaced \
            --disable-lavf \
            --disable-swscale \
            --enable-strip \
            --enable-pic \
            --bit-depth=8 \
            --chroma-format=420
        make -j"$jobs"
        make install
        popd >/dev/null

        pushd "$SRC_ROOT/ffmpeg" >/dev/null
        make distclean >/dev/null 2>&1 || true
        PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" ./configure \
            --prefix="$PREFIX" \
            --target-os=android \
            --arch=aarch64 \
            --cpu=armv8-a \
            --cc="$cc" \
            --cxx="$cxx" \
            --ar="$ar" \
            --nm="$nm" \
            --ranlib="$ranlib" \
            --strip="$strip" \
            --sysroot="$sysroot" \
            --enable-cross-compile \
            --pkg-config="$pkg_config_bin" \
            --pkg-config-flags="--static" \
            --enable-static \
            --disable-shared \
            --disable-programs \
            --disable-doc \
            --disable-debug \
            --disable-runtime-cpudetect \
            --enable-pic \
            --enable-small \
            --enable-hardcoded-tables \
            --disable-swscale-alpha \
            --disable-autodetect \
            --disable-network \
            --disable-everything \
            --disable-avdevice \
            --disable-postproc \
            --disable-swresample \
            --disable-bsfs \
            --disable-parser=ac3 \
            --disable-bsf=aac_adtstoasc \
            --disable-bsf=hevc_mp4toannexb \
            --disable-muxer=adts \
            --disable-muxer=latm \
            --enable-zlib \
            --enable-gpl \
            --enable-nonfree \
            --enable-libx264 \
            --enable-avcodec \
            --enable-avformat \
            --enable-avutil \
            --enable-avfilter \
            --enable-swscale \
            --enable-encoder=libx264 \
            --enable-decoder=h264,png \
            --enable-parser=h264 \
            --enable-demuxer=mpegts,image2 \
            --enable-muxer=mpegts \
            --enable-protocol=file \
            --enable-filter=overlay,scale,format,buffer,buffersink \
            --extra-cflags="-Oz -fPIC -ffunction-sections -fdata-sections -fvisibility=hidden -fomit-frame-pointer -fno-unwind-tables -fno-asynchronous-unwind-tables -I$PREFIX/include" \
            --extra-ldflags="-Wl,--gc-sections -L$PREFIX/lib"
        make -j"$jobs"
        make install
        popd >/dev/null
    fi

    if [[ ! -f "$PREFIX/lib/libavformat.a" || ! -f "$PREFIX/lib/libx264.a" || ! -f "$SRC_ROOT/ffmpeg/fftools/ffmpeg.c" ]]; then
        echo "Missing FFmpeg/x264 artifacts under $WORK_ROOT. Run without SKIP_DEPENDENCY_BUILD=1 first." >&2
        exit 1
    fi

    generate_link_project
    rm -rf "$BUILD_ROOT"
    cmake -S "$LINK_ROOT" -B "$BUILD_ROOT" -G Ninja \
        -DANDROID_ABI=arm64-v8a \
        -DANDROID_PLATFORM="android-$ANDROID_API" \
        -DCMAKE_TOOLCHAIN_FILE="$ndk_root/build/cmake/android.toolchain.cmake" \
        -DANDROID_NDK="$ndk_root" \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_ROOT"

    cp "$BUILD_ROOT/libts-watermark.so" "$OUTPUT_SO"
    "$strip" --strip-unneeded "$OUTPUT_SO"
    stat -f '%z %N' "$OUTPUT_SO" 2>/dev/null || stat -c '%s %n' "$OUTPUT_SO"
}

main "$@"
