package com.dongao.lib.tswatermark;

import static org.junit.Assert.assertArrayEquals;

import org.junit.Test;

public class FfmpegCommandBuilderTest {

    @Test
    public void buildWatermarkTsCommand_usesBitrateControlWhenProfileIsAvailable() {
        String[] command = FfmpegCommandBuilder.buildWatermarkTsCommand(
                "/tmp/input.ts",
                "/tmp/watermark.png",
                "/tmp/output.ts",
                WatermarkEncodingProfile.bitrateControlled(700000, 770000, 1400000, 900000));

        assertArrayEquals(new String[]{
                "ffmpeg",
                "-i", "/tmp/input.ts",
                "-i", "/tmp/watermark.png",
                "-filter_complex", "[0:v][1:v]overlay=10:10[vout]",
                "-map", "[vout]",
                "-map", "0:a?",
                "-c:v", "libx264",
                "-preset", "veryfast",
                "-pix_fmt", "yuv420p",
                "-c:a", "copy",
                "-b:v", "700000",
                "-maxrate", "770000",
                "-bufsize", "1400000",
                "-muxrate", "900000",
                "-f", "mpegts",
                "-y",
                "/tmp/output.ts"
        }, command);
    }

    @Test
    public void buildWatermarkTsCommand_fallsBackToCrfWhenProfileMissing() {
        String[] command = FfmpegCommandBuilder.buildWatermarkTsCommand(
                "/tmp/input.ts",
                "/tmp/watermark.png",
                "/tmp/output.ts",
                WatermarkEncodingProfile.fallback());

        assertArrayEquals(new String[]{
                "ffmpeg",
                "-i", "/tmp/input.ts",
                "-i", "/tmp/watermark.png",
                "-filter_complex", "[0:v][1:v]overlay=10:10[vout]",
                "-map", "[vout]",
                "-map", "0:a?",
                "-c:v", "libx264",
                "-preset", "veryfast",
                "-pix_fmt", "yuv420p",
                "-c:a", "copy",
                "-crf", "23",
                "-f", "mpegts",
                "-y",
                "/tmp/output.ts"
        }, command);
    }
}
