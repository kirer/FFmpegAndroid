package com.dongao.lib.tswatermark;

final class TargetBitrateCalculator {

    private static final int DEFAULT_AUDIO_BITRATE = 128_000;
    private static final int MIN_VIDEO_BITRATE = 64_000;
    private static final int MIN_BUFFER_SIZE = 256_000;
    private static final int MIN_MUX_OVERHEAD = 24_000;
    private static final double MUX_OVERHEAD_RATIO = 0.03d;

    private TargetBitrateCalculator() {
    }

    static WatermarkEncodingProfile createProfile(SegmentMediaInfo mediaInfo) {
        int reservedAudioBitrate = resolveAudioBitrate(mediaInfo);
        int targetVideoBitrate = resolveVideoBitrate(mediaInfo, reservedAudioBitrate);
        if (targetVideoBitrate <= 0) {
            return WatermarkEncodingProfile.fallback();
        }

        int maxVideoBitrate = Math.max(targetVideoBitrate, scaleBitrate(targetVideoBitrate, 1.10d));
        int videoBufferSize = Math.max(MIN_BUFFER_SIZE, targetVideoBitrate * 2);
        int muxRateBase = mediaInfo.getTotalBitrate() > 0
                ? mediaInfo.getTotalBitrate()
                : targetVideoBitrate + reservedAudioBitrate + estimateMuxOverhead(targetVideoBitrate + reservedAudioBitrate);
        int muxRate = Math.max(muxRateBase,
                targetVideoBitrate + reservedAudioBitrate + estimateMuxOverhead(muxRateBase));

        return WatermarkEncodingProfile.bitrateControlled(
                targetVideoBitrate,
                maxVideoBitrate,
                videoBufferSize,
                muxRate);
    }

    private static int resolveVideoBitrate(SegmentMediaInfo mediaInfo, int reservedAudioBitrate) {
        if (mediaInfo.getVideoBitrate() > 0) {
            return mediaInfo.getVideoBitrate();
        }
        if (mediaInfo.getTotalBitrate() <= 0) {
            return 0;
        }

        int target = mediaInfo.getTotalBitrate() - reservedAudioBitrate
                - estimateMuxOverhead(mediaInfo.getTotalBitrate());
        return Math.max(MIN_VIDEO_BITRATE, target);
    }

    private static int resolveAudioBitrate(SegmentMediaInfo mediaInfo) {
        if (!mediaInfo.hasAudio()) {
            return 0;
        }
        if (mediaInfo.getAudioBitrate() > 0) {
            return mediaInfo.getAudioBitrate();
        }
        return DEFAULT_AUDIO_BITRATE;
    }

    private static int estimateMuxOverhead(int bitrate) {
        return Math.max(MIN_MUX_OVERHEAD, scaleBitrate(bitrate, MUX_OVERHEAD_RATIO));
    }

    private static int scaleBitrate(int bitrate, double ratio) {
        return (int) Math.round(bitrate * ratio);
    }
}
