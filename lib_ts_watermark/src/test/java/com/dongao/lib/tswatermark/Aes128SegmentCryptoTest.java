package com.dongao.lib.tswatermark;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;

import org.junit.Test;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public class Aes128SegmentCryptoTest {

    @Test
    public void normalizeAesBytes_acceptsRawAndHexValues() {
        assertEquals("0123456789abcdef",
                new String(Aes128SegmentCrypto.normalizeAesBytes("0123456789abcdef", "key"),
                        StandardCharsets.UTF_8));
        assertEquals("0123456789abcdef",
                new String(Aes128SegmentCrypto.normalizeAesBytes("30313233343536373839616263646566", "key"),
                        StandardCharsets.UTF_8));
    }

    @Test
    public void encryptThenDecrypt_roundTripsBytes() throws Exception {
        File tempDir = Files.createTempDirectory("ts-crypto-test").toFile();
        File plain = new File(tempDir, "plain.ts");
        File encrypted = new File(tempDir, "encrypted.ts");
        File decrypted = new File(tempDir, "decrypted.ts");
        byte[] sample = "sample transport stream payload".getBytes(StandardCharsets.UTF_8);
        Files.write(plain.toPath(), sample);

        try {
            Aes128SegmentCrypto.encrypt(plain, encrypted, "0123456789abcdef",
                    "30313233343536373839616263646566");
            Aes128SegmentCrypto.decrypt(encrypted, decrypted, "0123456789abcdef",
                    "30313233343536373839616263646566");
            assertArrayEquals(sample, Files.readAllBytes(decrypted.toPath()));
        } finally {
            Aes128SegmentCrypto.deleteRecursively(tempDir);
        }
    }
}
