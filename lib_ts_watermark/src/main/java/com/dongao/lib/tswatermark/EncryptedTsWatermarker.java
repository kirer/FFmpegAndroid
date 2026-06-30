package com.dongao.lib.tswatermark;

import java.io.File;

public final class EncryptedTsWatermarker {

    private EncryptedTsWatermarker() {
    }

    public static void watermarkSegment(String encryptedTsPath, String watermarkPath,
                                        String outputPath, String key, String iv) throws Exception {
        watermarkSegment(new File(encryptedTsPath), new File(watermarkPath), new File(outputPath), key, iv);
    }

    public static void watermarkSegment(File encryptedTs, File watermarkFile,
                                        File outputFile, String key, String iv) throws Exception {
        validateInput(encryptedTs, "encrypted ts");
        validateInput(watermarkFile, "watermark");

        File parent = outputFile.getParentFile();
        if (parent == null) {
            throw new IllegalArgumentException("output file must have a parent directory");
        }
        if (!parent.exists() && !parent.mkdirs()) {
            throw new IllegalStateException("failed to create output directory: " + parent.getAbsolutePath());
        }

        File workspace = new File(parent, ".ts-watermark-" + System.currentTimeMillis());
        if (!workspace.mkdirs() && !workspace.isDirectory()) {
            throw new IllegalStateException("failed to create temp workspace: " + workspace.getAbsolutePath());
        }

        try {
            File plainInput = new File(workspace, "input_plain.ts");
            File plainOutput = new File(workspace, "output_plain.ts");

            Aes128SegmentCrypto.decrypt(encryptedTs, plainInput, key, iv);
            SegmentMediaInfo mediaInfo = SegmentMediaInfoReader.read(plainInput);
            WatermarkEncodingProfile encodingProfile = TargetBitrateCalculator.createProfile(mediaInfo);
            String[] command = FfmpegCommandBuilder.buildWatermarkTsCommand(
                    plainInput.getAbsolutePath(),
                    watermarkFile.getAbsolutePath(),
                    plainOutput.getAbsolutePath(),
                    encodingProfile);
            int result = NativeFfmpegRunner.runCommand(command);
            if (result != 0) {
                throw new IllegalStateException("ffmpeg watermark failed with exit code " + result);
            }
            Aes128SegmentCrypto.encrypt(plainOutput, outputFile, key, iv);
        } finally {
            Aes128SegmentCrypto.deleteRecursively(workspace);
        }
    }

    private static void validateInput(File file, String label) {
        if (file == null || !file.isFile()) {
            throw new IllegalArgumentException(label + " file does not exist: " + file);
        }
    }
}
