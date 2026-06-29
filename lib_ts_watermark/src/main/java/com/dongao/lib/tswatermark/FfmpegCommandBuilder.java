package com.dongao.lib.tswatermark;

final class FfmpegCommandBuilder {

    private FfmpegCommandBuilder() {
    }

    static String[] buildWatermarkTsCommand(String inputPath, String watermarkPath, String outputPath) {
        return new String[]{
                "ffmpeg",
                "-i", inputPath,
                "-i", watermarkPath,
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
                outputPath
        };
    }
}
