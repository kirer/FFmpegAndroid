package com.dongao.lib.ffmpeg;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class ThreadPoolUtil {
    public static final ThreadPoolUtil INSTANCE = new ThreadPoolUtil();
    private final ExecutorService singleThreadPool = Executors.newSingleThreadExecutor();

    public void executeSingleThreadPool(Runnable runnable) {
        singleThreadPool.execute(runnable);
    }
}