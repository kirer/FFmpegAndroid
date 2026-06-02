/**
 * Note: factory of video render
 * Date: 2026/6/1
 * Author: frank
 */

#ifndef VIDEO_RENDER_FACTORY_H
#define VIDEO_RENDER_FACTORY_H

#include <memory>

#include "render/opengl/OpenglVideoRender.h"

#if defined(__ANDROID__)
#include "render/android/MediacodecRender.h"
#endif
#if defined(__APPLE__)
#include "render/metal/MetalVideoRender.h"
#include "render/ios/SampleBufferRender.h"
#endif

#include "VideoRender.h"

class VideoRendererFactory {
public:
    VideoRendererFactory() = default;

    ~VideoRendererFactory() = default;

    static std::unique_ptr<VideoRender>
    CreateVideoRenderer(VideoRenderType videoRenderType, const int &sessionID = 0) {
        std::unique_ptr<VideoRender> renderer = nullptr;

        switch (videoRenderType) {
            case VIDEO_RENDER_MEDIACODEC:
#if defined(__ANDROID__)
                renderer = std::make_unique<MediaCodecRender>();
#endif
                break;
            case VIDEO_RENDER_OPENGL:
                renderer = std::make_unique<OpenGLVideoRender>();
                break;
            case VIDEO_RENDER_METAL:
#if defined(__APPLE__)
                renderer = std::make_unique<MetalVideoRenderer>(sessionID);
#endif
                break;
            case VIDEO_RENDER_SAMPLEBUFFER:
#if defined(__APPLE__)
                renderer = std::make_unique<AVSBDLVideoRenderer>(sessionID);
#endif
                break;
            case VIDEO_RENDER_UNKNOWN:
            default:
                break;
        }

        return renderer;
    }

};

#endif
