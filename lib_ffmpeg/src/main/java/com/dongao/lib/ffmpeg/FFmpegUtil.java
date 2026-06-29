package com.dongao.lib.ffmpeg;

/**
 * FFmpeg watermark command builders
 */
public class FFmpegUtil {

    private static String[] insert(String[] cmd, int position, String inputPath) {
        return insert(cmd, position, inputPath, null);
    }

    private static String[] insert(String[] cmd, int position, String inputPath, String outputPath) {
        if (cmd == null || inputPath == null || position < 2) return cmd;
        int len = (outputPath != null ? (cmd.length + 2) : (cmd.length + 1));
        String[] result = new String[len];
        System.arraycopy(cmd, 0, result, 0, position);
        result[position] = inputPath;
        System.arraycopy(cmd, position, result, position + 1, cmd.length - position);
        if (outputPath != null) result[result.length - 1] = outputPath;
        return result;
    }

    public static String[] insert(String[] cmd, int position1, String inputPath1,
                                  int position2, String inputPath2, String outputPath) {
        if (cmd == null || inputPath1 == null || position1 < 2 || inputPath2 == null || position2 < 4)
            return cmd;
        int len = (outputPath != null ? (cmd.length + 3) : (cmd.length + 2));
        String[] result = new String[len];
        System.arraycopy(cmd, 0, result, 0, position1);
        result[position1] = inputPath1;
        System.arraycopy(cmd, position1, result, position1 + 1, position2 - position1 - 1);
        result[position2] = inputPath2;
        System.arraycopy(cmd, position2 - 1, result, position2 + 1, cmd.length - (position2 - 1));
        if (outputPath != null) result[result.length - 1] = outputPath;
        return result;
    }

    private static String obtainOverlay(int offsetX, int offsetY, int location) {
        switch (location) {
            case 2:
                return "overlay='(main_w-overlay_w)-" + offsetX + ":" + offsetY + "'";
            case 3:
                return "overlay='" + offsetX + ":(main_h-overlay_h)-" + offsetY + "'";
            case 4:
                return "overlay='(main_w-overlay_w)-" + offsetX + ":(main_h-overlay_h)-" + offsetY + "'";
            case 1:
            default:
                return "overlay=" + offsetX + ":" + offsetY;
        }
    }

    // ========== Normal video watermark ==========

    public static String[] addWaterMarkImg(String inputPath, String imgPath, int location,
                                           int offsetXY, String outputPath) {
        String overlay = obtainOverlay(offsetXY, offsetXY, location);
        String waterMarkCmd = "ffmpeg -i -i -c:v libx264 -crf 23 -filter_complex %s -map 0:a? -c:a copy -y";
        waterMarkCmd = String.format(waterMarkCmd, overlay);
        return insert(waterMarkCmd.split(" "), 2, inputPath, 4, imgPath, outputPath);
    }

    public static String[] addWaterMarkImg(String inputPath, String imgPath, int location,
                                           int offsetXY, String enableExpr, String outputPath) {
        String overlay = obtainOverlay(offsetXY, offsetXY, location);
        if (enableExpr != null && !enableExpr.isEmpty()) {
            overlay += ":enable='" + enableExpr + "'";
        }
        String waterMarkCmd = "ffmpeg -i -i -c:v libx264 -crf 23 -filter_complex %s -map 0:a? -c:a copy -y";
        waterMarkCmd = String.format(waterMarkCmd, overlay);
        return insert(waterMarkCmd.split(" "), 2, inputPath, 4, imgPath, outputPath);
    }

    public static String[] addWaterMarkGif(String inputPath, String gifPath, int location,
                                           int offsetXY, String outputPath) {
        String overlay = obtainOverlay(offsetXY, offsetXY, location) + ":shortest=1";
        String waterMarkCmd = "ffmpeg -i -ignore_loop 0 -i -c:v libx264 -crf 23 -filter_complex %s -map 0:a? -c:a copy -y";
        waterMarkCmd = String.format(waterMarkCmd, overlay);
        return insert(waterMarkCmd.split(" "), 2, inputPath, 6, gifPath, outputPath);
    }

    public static String[] addWaterMarkMulti(String inputPath, String[] imgPaths,
                                             int[] locations, int[] offsets, String outputPath) {
        if (imgPaths == null || imgPaths.length == 0) return null;
        StringBuilder filter = new StringBuilder();
        String prevLabel = "0:v";
        for (int i = 0; i < imgPaths.length && i < 3; i++) {
            String overlay = obtainOverlay(offsets[i], offsets[i], locations[i]);
            String outLabel = (i == imgPaths.length - 1) ? "outv" : ("tmp" + i);
            filter.append("[").append(prevLabel).append("][").append(i + 1).append(":v]")
                    .append(overlay).append("[").append(outLabel).append("]");
            if (i < imgPaths.length - 1) filter.append(";");
            prevLabel = outLabel;
        }
        StringBuilder cmd = new StringBuilder("ffmpeg -i ");
        for (String ignored : imgPaths) cmd.append("-i ");
        cmd.append("-c:v libx264 -crf 23 -filter_complex \"")
                .append(filter)
                .append("\" -map \"[outv]\" -map 0:a? -c:a copy -y");
        String[] parts = cmd.toString().split(" ");
        String[] result = insert(parts, 2, inputPath);
        int offset = 3;
        for (int i = 0; i < imgPaths.length; i++) {
            result = insert(result, offset + i * 2, imgPaths[i]);
        }
        String[] finalResult = new String[result.length + 1];
        System.arraycopy(result, 0, finalResult, 0, result.length);
        finalResult[finalResult.length - 1] = outputPath;
        return finalResult;
    }

    public static String[] addWaterMarkTs(String inputPath, String imgPath, int location,
                                          int offsetXY, String outputPath) {
        String overlay = "[0:v][1:v]" + obtainOverlay(offsetXY, offsetXY, location) + "[vout]";
        return new String[]{
                "ffmpeg",
                "-i", inputPath, "-i", imgPath,
                "-c:v", "libx264",
                "-preset", "veryfast",
                "-crf", "23",
                "-pix_fmt", "yuv420p",
                "-filter_complex", overlay,
                "-map", "[vout]",
                "-map", "0:a?", "-c:a", "copy",
                "-f", "mpegts", "-y",
                outputPath
        };
    }
}
