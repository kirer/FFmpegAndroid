package com.dongao.lib.ffmpeg;

import android.util.Log;

import java.io.File;
import java.util.List;

/**
 * JNI interface for FFmpeg command execution (watermark-focused)
 */
public class FFmpegCmd {

    static {
        System.loadLibrary("media-handle");
    }

    private final static String TAG = "FFmpegCmd";

    private static OnHandleListener mProgressListener;

    public static void execute(final String[] commands, final OnHandleListener onHandleListener) {
        mProgressListener = onHandleListener;
        ThreadPoolUtil.INSTANCE.executeSingleThreadPool(new Runnable() {
            @Override
            public void run() {
                if (onHandleListener != null) {
                    onHandleListener.onBegin();
                }
                int result = handle(commands);
                if (onHandleListener != null) {
                    onHandleListener.onEnd(result, "");
                }
                mProgressListener = null;
            }
        });
    }

    public static int executeSync(final String[] commands) {
        return handle(commands);
    }

    public static void execute(final List<String[]> commands, final OnHandleListener onHandleListener) {
        mProgressListener = onHandleListener;
        ThreadPoolUtil.INSTANCE.executeSingleThreadPool(new Runnable() {
            @Override
            public void run() {
                if (onHandleListener != null) {
                    onHandleListener.onBegin();
                }
                int result = 0;
                for (String[] command : commands) {
                    result = handle(command);
                    Log.i(TAG, "result=" + result);
                }
                if (onHandleListener != null) {
                    onHandleListener.onEnd(result, "");
                }
                mProgressListener = null;
            }
        });
    }

    public static void cancelTask(boolean cancel) {
        cancelTaskJni(cancel ? 1 : 0);
    }

    private native static int handle(String[] commands);

    private native static void cancelTaskJni(int cancel);
    /**
     * Watermark a single encrypted TS segment by decrypting it locally, running
     * the watermark pass on the plaintext TS, then re-encrypting the output.
     */
    public static int watermarkEncryptedTs(String tsPath, String wmPath, String outputPath, String keyInput, String ivInput) {
        File workspace = null;
        try {
            File outputFile = new File(outputPath);
            File parent = outputFile.getParentFile();
            if (parent == null) {
                Log.e(TAG, "outputPath has no parent: " + outputPath);
                return -1;
            }
            if (!parent.exists() && !parent.mkdirs()) {
                Log.e(TAG, "failed to create output dir: " + parent.getAbsolutePath());
                return -1;
            }

            workspace = new File(parent, ".encrypted-ts-" + System.currentTimeMillis());
            if (!workspace.mkdirs() && !workspace.isDirectory()) {
                Log.e(TAG, "failed to create temp workspace: " + workspace.getAbsolutePath());
                return -1;
            }

            File plainInput = new File(workspace, "input_plain.ts");
            File watermarkedPlain = new File(workspace, "watermarked_plain.ts");

            EncryptedTsUtil.decryptFile(new File(tsPath), plainInput, keyInput, ivInput);
            String[] cmd = FFmpegUtil.addWaterMarkTs(plainInput.getAbsolutePath(), wmPath, 1, 10,
                    watermarkedPlain.getAbsolutePath());
            int result = executeSync(cmd);
            if (result != 0) {
                Log.e(TAG, "ffmpeg watermark failed, code=" + result);
                return result;
            }

            EncryptedTsUtil.encryptFile(watermarkedPlain, outputFile, keyInput, ivInput);
            return 0;
        } catch (Exception e) {
            Log.e(TAG, "watermarkEncryptedTs failed", e);
            return -1;
        } finally {
            EncryptedTsUtil.deleteQuietly(workspace);
        }
    }

    public static void onProgressCallback(int position, int duration, int state) {
        if (position > duration && duration > 0) return;
        if (mProgressListener != null) {
            if (position > 0 && duration > 0) {
                int progress = position * 100 / duration;
                if (progress < 100) {
                    mProgressListener.onProgress(progress, duration);
                }
            } else {
                mProgressListener.onProgress(position, duration);
            }
        }
    }

    public static void onMsgCallback(String msg, int level) {
        if (msg != null && !msg.isEmpty()) {
            Log.e(TAG, "from native msg=" + msg);
        }
    }
}
