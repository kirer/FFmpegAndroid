package com.dongao.lib.tswatermark;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class TargetBitrateCalculatorTest {

    @Test
    public void createProfile_prefersInputVideoBitrateWhenAvailable() {
        SegmentMediaInfo mediaInfo = new SegmentMediaInfo(
                6000L,
                1_000_000,
                720_000,
                128_000,
                true);

        WatermarkEncodingProfile profile = TargetBitrateCalculator.createProfile(mediaInfo);

        assertTrue(profile.isBitrateControlled());
        assertEquals(720_000, profile.getVideoBitrate());
        assertEquals(792_000, profile.getMaxVideoBitrate());
        assertEquals(1_440_000, profile.getVideoBufferSize());
        assertEquals(1_000_000, profile.getMuxRate());
    }

    @Test
    public void createProfile_estimatesVideoBitrateFromTotalBitrate() {
        SegmentMediaInfo mediaInfo = new SegmentMediaInfo(
                6000L,
                900_000,
                0,
                128_000,
                true);

        WatermarkEncodingProfile profile = TargetBitrateCalculator.createProfile(mediaInfo);

        assertTrue(profile.isBitrateControlled());
        assertEquals(745_000, profile.getVideoBitrate());
        assertEquals(819_500, profile.getMaxVideoBitrate());
        assertEquals(1_490_000, profile.getVideoBufferSize());
        assertEquals(900_000, profile.getMuxRate());
    }

    @Test
    public void createProfile_fallsBackWhenNoBitrateInfoExists() {
        SegmentMediaInfo mediaInfo = new SegmentMediaInfo(
                0L,
                0,
                0,
                0,
                false);

        WatermarkEncodingProfile profile = TargetBitrateCalculator.createProfile(mediaInfo);

        assertFalse(profile.isBitrateControlled());
        assertEquals(23, profile.getFallbackCrf());
    }
}
