cmake_minimum_required(VERSION 3.4.1)
project(ts_watermark)

set(EXTRA ../../../../extra)
set(EXTRA_FFMPEG ${EXTRA}/ffmpeg)
set(SHARED_CPP ../../../../lib_ffmpeg/src/main/cpp)

set(SRC_FFMPEG
        ${SHARED_CPP}/ffmpeg/cmdutils.c
        ${SHARED_CPP}/ffmpeg/ffmpeg.c
        ${SHARED_CPP}/ffmpeg/ffmpeg_demux.c
        ${SHARED_CPP}/ffmpeg/ffmpeg_filter.c
        ${SHARED_CPP}/ffmpeg/ffmpeg_hw.c
        ${SHARED_CPP}/ffmpeg/ffmpeg_mux.c
        ${SHARED_CPP}/ffmpeg/ffmpeg_mux_init.c
        ${SHARED_CPP}/ffmpeg/ffmpeg_opt.c
        ${SHARED_CPP}/ffmpeg/objpool.c
        ${SHARED_CPP}/ffmpeg/opt_common.c
        ${SHARED_CPP}/ffmpeg/sync_queue.c
        ${SHARED_CPP}/ffmpeg/thread_queue.c)

add_library(ts-watermark SHARED
        ${SRC_FFMPEG}
        ffmpeg_runner.c)

add_library(ffmpeg SHARED IMPORTED)
set_target_properties(ffmpeg PROPERTIES IMPORTED_LOCATION
        ${CMAKE_SOURCE_DIR}/${EXTRA}/libs/${CMAKE_ANDROID_ARCH_ABI}/libffmpeg.so)

include_directories(${SHARED_CPP})
include_directories(${EXTRA_FFMPEG}/include)

if(${CMAKE_ANDROID_ARCH_ABI} MATCHES "armeabi-v7a")
    include_directories(${EXTRA_FFMPEG}/include/armeabi-v7a/libavcodec)
    include_directories(${EXTRA_FFMPEG}/include/armeabi-v7a/libavutil)
elseif(${CMAKE_ANDROID_ARCH_ABI} MATCHES "arm64-v8a")
    include_directories(${EXTRA_FFMPEG}/include/arm64-v8a/libavcodec)
    include_directories(${EXTRA_FFMPEG}/include/arm64-v8a/libavutil)
endif()

find_library(log-lib log)

target_link_libraries(ts-watermark ffmpeg ${log-lib})
