package com.dongao.lib.tswatermark;

final class SegmentMediaInfo {

    private final long durationMs;
    private final int totalBitrate;
    private final int videoBitrate;
    private final int audioBitrate;
    private final boolean hasAudio;

    SegmentMediaInfo(long durationMs, int totalBitrate, int videoBitrate, int audioBitrate, boolean hasAudio) {
        this.durationMs = durationMs;
        this.totalBitrate = totalBitrate;
        this.videoBitrate = videoBitrate;
        this.audioBitrate = audioBitrate;
        this.hasAudio = hasAudio;
    }

    long getDurationMs() {
        return durationMs;
    }

    int getTotalBitrate() {
        return totalBitrate;
    }

    int getVideoBitrate() {
        return videoBitrate;
    }

    int getAudioBitrate() {
        return audioBitrate;
    }

    boolean hasAudio() {
        return hasAudio;
    }
}
