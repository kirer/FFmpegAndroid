package com.dongao.lib.tswatermark;

import java.util.ArrayList;
import java.util.List;

final class FfmpegCommandBuilder {

    private FfmpegCommandBuilder() {
    }

    static String[] buildWatermarkTsCommand(String inputPath, String watermarkPath, String outputPath,
                                            WatermarkEncodingProfile encodingProfile) {
        List<String> command = new ArrayList<>();
        command.add("ffmpeg");
        command.add("-i");
        command.add(inputPath);
        command.add("-i");
        command.add(watermarkPath);
        command.add("-filter_complex");
        command.add("[0:v][1:v]overlay=10:10[vout]");
        command.add("-map");
        command.add("[vout]");
        command.add("-map");
        command.add("0:a?");
        command.add("-c:v");
        command.add("libx264");
        command.add("-preset");
        command.add("veryfast");
        command.add("-pix_fmt");
        command.add("yuv420p");
        command.add("-c:a");
        command.add("copy");

        if (encodingProfile != null && encodingProfile.isBitrateControlled()) {
            command.add("-b:v");
            command.add(String.valueOf(encodingProfile.getVideoBitrate()));
            command.add("-maxrate");
            command.add(String.valueOf(encodingProfile.getMaxVideoBitrate()));
            command.add("-bufsize");
            command.add(String.valueOf(encodingProfile.getVideoBufferSize()));
            command.add("-muxrate");
            command.add(String.valueOf(encodingProfile.getMuxRate()));
        } else {
            command.add("-crf");
            command.add(String.valueOf(WatermarkEncodingProfile.fallback().getFallbackCrf()));
        }

        command.add("-f");
        command.add("mpegts");
        command.add("-y");
        command.add(outputPath);
        return command.toArray(new String[0]);
    }
}
