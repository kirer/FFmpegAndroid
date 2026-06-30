package com.dongao.lib.tswatermark;

import java.io.File;

public final class EncryptedTsWatermarker {

    static {
        System.loadLibrary("ts-watermark");
    }

    private EncryptedTsWatermarker() {
    }

    public static void watermarkSegment(String encryptedTsPath, String watermarkPath,
                                        String outputPath, String key, String iv) {
        nativeWatermarkSegment(
                requireText(encryptedTsPath, "encryptedTsPath"),
                requireText(watermarkPath, "watermarkPath"),
                requireText(outputPath, "outputPath"),
                requireText(key, "key"),
                requireText(iv, "iv"));
    }

    public static void watermarkSegment(File encryptedTs, File watermarkFile,
                                        File outputFile, String key, String iv) {
        if (encryptedTs == null) {
            throw new IllegalArgumentException("encryptedTs is null");
        }
        if (watermarkFile == null) {
            throw new IllegalArgumentException("watermarkFile is null");
        }
        if (outputFile == null) {
            throw new IllegalArgumentException("outputFile is null");
        }
        watermarkSegment(
                encryptedTs.getAbsolutePath(),
                watermarkFile.getAbsolutePath(),
                outputFile.getAbsolutePath(),
                key,
                iv);
    }

    private static String requireText(String value, String label) {
        if (value == null) {
            throw new IllegalArgumentException(label + " is null");
        }
        String normalized = value.trim();
        if (normalized.isEmpty()) {
            throw new IllegalArgumentException(label + " is empty");
        }
        return normalized;
    }

    private static native void nativeWatermarkSegment(String encryptedTsPath, String watermarkPath,
                                                      String outputPath, String key, String iv);
}
