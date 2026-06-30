package com.dongao.lib.tswatermark;

final class WatermarkEncodingProfile {

    private static final int FALLBACK_CRF = 23;

    private final boolean bitrateControlled;
    private final int videoBitrate;
    private final int maxVideoBitrate;
    private final int videoBufferSize;
    private final int muxRate;

    private WatermarkEncodingProfile(boolean bitrateControlled, int videoBitrate,
                                     int maxVideoBitrate, int videoBufferSize, int muxRate) {
        this.bitrateControlled = bitrateControlled;
        this.videoBitrate = videoBitrate;
        this.maxVideoBitrate = maxVideoBitrate;
        this.videoBufferSize = videoBufferSize;
        this.muxRate = muxRate;
    }

    static WatermarkEncodingProfile fallback() {
        return new WatermarkEncodingProfile(false, 0, 0, 0, 0);
    }

    static WatermarkEncodingProfile bitrateControlled(int videoBitrate, int maxVideoBitrate,
                                                      int videoBufferSize, int muxRate) {
        return new WatermarkEncodingProfile(true, videoBitrate, maxVideoBitrate, videoBufferSize, muxRate);
    }

    boolean isBitrateControlled() {
        return bitrateControlled;
    }

    int getVideoBitrate() {
        return videoBitrate;
    }

    int getMaxVideoBitrate() {
        return maxVideoBitrate;
    }

    int getVideoBufferSize() {
        return videoBufferSize;
    }

    int getMuxRate() {
        return muxRate;
    }

    int getFallbackCrf() {
        return FALLBACK_CRF;
    }
}
