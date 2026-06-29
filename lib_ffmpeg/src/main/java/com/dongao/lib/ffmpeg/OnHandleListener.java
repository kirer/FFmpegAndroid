package com.dongao.lib.ffmpeg;

public interface OnHandleListener {
    void onBegin();
    void onEnd(int resultCode, String resultMsg);
    void onProgress(int progress, int duration);
    void onMsg(String msg);
}