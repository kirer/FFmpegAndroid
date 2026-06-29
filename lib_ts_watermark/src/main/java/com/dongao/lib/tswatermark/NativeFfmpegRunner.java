package com.dongao.lib.tswatermark;

final class NativeFfmpegRunner {

    static {
        System.loadLibrary("ts-watermark");
    }

    private NativeFfmpegRunner() {
    }

    static native int runCommand(String[] command);
}
