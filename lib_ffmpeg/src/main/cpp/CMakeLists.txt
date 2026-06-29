cmake_minimum_required(VERSION 3.4.1)

set(EXTRA ../../../../extra)
set(EXTRA_FFMPEG ${EXTRA}/ffmpeg)

set(SRC_FFMPEG
        ffmpeg/cmdutils.c
        ffmpeg/ffmpeg.c
        ffmpeg/ffmpeg_demux.c
        ffmpeg/ffmpeg_filter.c
        ffmpeg/ffmpeg_hw.c
        ffmpeg/ffmpeg_mux.c
        ffmpeg/ffmpeg_mux_init.c
        ffmpeg/ffmpeg_opt.c
        ffmpeg/objpool.c
        ffmpeg/opt_common.c
        ffmpeg/sync_queue.c
        ffmpeg/thread_queue.c)

add_library(media-handle SHARED
        ${SRC_FFMPEG}
        ffmpeg_cmd.c)

add_library(ffmpeg SHARED IMPORTED)
set_target_properties(ffmpeg PROPERTIES IMPORTED_LOCATION
        ${CMAKE_SOURCE_DIR}/${EXTRA}/libs/${CMAKE_ANDROID_ARCH_ABI}/libffmpeg.so)

include_directories(.)
include_directories(${EXTRA_FFMPEG}/include)

if(${CMAKE_ANDROID_ARCH_ABI} MATCHES "armeabi-v7a")
    include_directories(${EXTRA_FFMPEG}/include/armeabi-v7a/libavcodec)
    include_directories(${EXTRA_FFMPEG}/include/armeabi-v7a/libavutil)
elseif(${CMAKE_ANDROID_ARCH_ABI} MATCHES "arm64-v8a")
    include_directories(${EXTRA_FFMPEG}/include/arm64-v8a/libavcodec)
    include_directories(${EXTRA_FFMPEG}/include/arm64-v8a/libavutil)
endif()

find_library(log-lib log)

target_link_libraries(media-handle ffmpeg ${log-lib})