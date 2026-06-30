package com.dongao.lib.tswatermark;

import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.media.MediaMetadataRetriever;

import java.io.File;
import java.io.IOException;

final class SegmentMediaInfoReader {

    private SegmentMediaInfoReader() {
    }

    static SegmentMediaInfo read(File inputFile) {
        long durationMs = 0L;
        int totalBitrate = 0;
        int videoBitrate = 0;
        int audioBitrate = 0;
        boolean hasAudio = false;

        MediaMetadataRetriever retriever = new MediaMetadataRetriever();
        try {
            retriever.setDataSource(inputFile.getAbsolutePath());
            durationMs = parseLong(retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION));
            totalBitrate = clampToPositiveInt(parseLong(
                    retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_BITRATE)));
        } catch (RuntimeException ignored) {
            // Some TS fragments expose incomplete metadata. We fall back to MediaExtractor and file-size estimates.
        } finally {
            try {
                retriever.release();
            } catch (IOException ignored) {
                // Ignore release failures from platform implementations.
            }
        }

        MediaExtractor extractor = new MediaExtractor();
        try {
            extractor.setDataSource(inputFile.getAbsolutePath());
            int trackCount = extractor.getTrackCount();
            for (int i = 0; i < trackCount; i++) {
                MediaFormat format = extractor.getTrackFormat(i);
                String mime = format.getString(MediaFormat.KEY_MIME);
                if (mime == null) {
                    continue;
                }

                long trackDurationUs = getLong(format, MediaFormat.KEY_DURATION);
                if (trackDurationUs > 0) {
                    durationMs = Math.max(durationMs, trackDurationUs / 1000L);
                }

                int trackBitrate = clampToPositiveInt(getLong(format, MediaFormat.KEY_BIT_RATE));
                if (mime.startsWith("video/")) {
                    videoBitrate = Math.max(videoBitrate, trackBitrate);
                } else if (mime.startsWith("audio/")) {
                    hasAudio = true;
                    audioBitrate += trackBitrate;
                }
            }
        } catch (IOException | RuntimeException ignored) {
            // We still have file-size estimation as a last fallback.
        } finally {
            extractor.release();
        }

        if (totalBitrate <= 0 && durationMs > 0) {
            totalBitrate = estimateTotalBitrate(inputFile.length(), durationMs);
        }

        return new SegmentMediaInfo(durationMs, totalBitrate, videoBitrate, audioBitrate, hasAudio);
    }

    private static long parseLong(String value) {
        if (value == null || value.trim().isEmpty()) {
            return 0L;
        }
        try {
            return Long.parseLong(value.trim());
        } catch (NumberFormatException ignored) {
            return 0L;
        }
    }

    private static long getLong(MediaFormat format, String key) {
        if (format == null || !format.containsKey(key)) {
            return 0L;
        }
        try {
            return format.getLong(key);
        } catch (ClassCastException ignored) {
            return 0L;
        }
    }

    private static int estimateTotalBitrate(long fileSizeBytes, long durationMs) {
        if (fileSizeBytes <= 0 || durationMs <= 0) {
            return 0;
        }
        long bitsPerSecond = (fileSizeBytes * 8_000L) / durationMs;
        return clampToPositiveInt(bitsPerSecond);
    }

    private static int clampToPositiveInt(long value) {
        if (value <= 0L) {
            return 0;
        }
        return value > Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) value;
    }
}
