package com.dongao.lib.tswatermark;

import static org.junit.Assert.assertArrayEquals;

import org.junit.Test;

public class FfmpegCommandBuilderTest {

    @Test
    public void buildWatermarkTsCommand_usesExpectedMappingAndOutput() {
        String[] command = FfmpegCommandBuilder.buildWatermarkTsCommand(
                "/tmp/input.ts",
                "/tmp/watermark.png",
                "/tmp/output.ts");

        assertArrayEquals(new String[]{
                "ffmpeg",
                "-i", "/tmp/input.ts",
                "-i", "/tmp/watermark.png",
                "-filter_complex", "[0:v][1:v]overlay=10:10[vout]",
                "-map", "[vout]",
                "-map", "0:a?",
                "-c:v", "libx264",
                "-preset", "veryfast",
                "-crf", "23",
                "-pix_fmt", "yuv420p",
                "-c:a", "copy",
                "-f", "mpegts",
                "-y",
                "/tmp/output.ts"
        }, command);
    }
}
