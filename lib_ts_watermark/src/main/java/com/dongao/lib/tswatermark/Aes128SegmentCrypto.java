package com.dongao.lib.tswatermark;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

import javax.crypto.Cipher;
import javax.crypto.CipherInputStream;
import javax.crypto.CipherOutputStream;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

final class Aes128SegmentCrypto {

    private static final int AES_BLOCK_SIZE = 16;
    private static final int BUFFER_SIZE = 16 * 1024;

    private Aes128SegmentCrypto() {
    }

    static void decrypt(File input, File output, String keyInput, String ivInput) throws Exception {
        transform(input, output, Cipher.DECRYPT_MODE, keyInput, ivInput);
    }

    static void encrypt(File input, File output, String keyInput, String ivInput) throws Exception {
        transform(input, output, Cipher.ENCRYPT_MODE, keyInput, ivInput);
    }

    static void deleteRecursively(File file) {
        if (file == null || !file.exists()) {
            return;
        }
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursively(child);
                }
            }
        }
        file.delete();
    }

    static byte[] normalizeAesBytes(String value, String label) {
        if (value == null) {
            throw new IllegalArgumentException(label + " is null");
        }
        String trimmed = value.trim();
        if (trimmed.isEmpty()) {
            throw new IllegalArgumentException(label + " is empty");
        }
        if (isHex(trimmed)) {
            byte[] decoded = decodeHex(trimmed);
            if (decoded.length == AES_BLOCK_SIZE) {
                return decoded;
            }
        }

        byte[] rawBytes = trimmed.getBytes(StandardCharsets.UTF_8);
        if (rawBytes.length == AES_BLOCK_SIZE) {
            return rawBytes;
        }
        throw new IllegalArgumentException(label + " must be 16 raw bytes or 32 hex chars");
    }

    private static void transform(File input, File output, int mode, String keyInput, String ivInput) throws Exception {
        SecretKeySpec keySpec = new SecretKeySpec(normalizeAesBytes(keyInput, "key"), "AES");
        IvParameterSpec ivSpec = new IvParameterSpec(normalizeAesBytes(ivInput, "iv"));
        Cipher cipher = Cipher.getInstance("AES/CBC/PKCS5Padding");
        cipher.init(mode, keySpec, ivSpec);

        if (mode == Cipher.ENCRYPT_MODE) {
            try (FileInputStream inputStream = new FileInputStream(input);
                 FileOutputStream outputStream = new FileOutputStream(output);
                 CipherOutputStream cipherOutputStream = new CipherOutputStream(outputStream, cipher)) {
                copy(inputStream, cipherOutputStream);
            }
            return;
        }

        try (FileInputStream inputStream = new FileInputStream(input);
             CipherInputStream cipherInputStream = new CipherInputStream(inputStream, cipher);
             FileOutputStream outputStream = new FileOutputStream(output)) {
            copy(cipherInputStream, outputStream);
        }
    }

    private static boolean isHex(String value) {
        if ((value.length() & 1) != 0) {
            return false;
        }
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            boolean isDigit = c >= '0' && c <= '9';
            boolean isLower = c >= 'a' && c <= 'f';
            boolean isUpper = c >= 'A' && c <= 'F';
            if (!isDigit && !isLower && !isUpper) {
                return false;
            }
        }
        return true;
    }

    private static byte[] decodeHex(String value) {
        byte[] result = new byte[value.length() / 2];
        for (int i = 0; i < value.length(); i += 2) {
            result[i / 2] = (byte) ((Character.digit(value.charAt(i), 16) << 4)
                    + Character.digit(value.charAt(i + 1), 16));
        }
        return result;
    }

    private static void copy(java.io.InputStream input, java.io.OutputStream output) throws IOException {
        byte[] buffer = new byte[BUFFER_SIZE];
        int read;
        while ((read = input.read(buffer)) != -1) {
            output.write(buffer, 0, read);
        }
        output.flush();
    }
}
