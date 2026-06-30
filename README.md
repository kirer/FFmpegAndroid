# FFmpegAndroid

当前仓库已经收敛成一个最小 Android demo，目标功能是：

- 输入单个 `AES-128` 加密的 `.ts`
- 使用已知 `key` 和 `iv` 解密
- 对该分片叠加水印
- 再重新加密输出

当前有效模块只有：

- `demo`
- `lib_ts_watermark`

原生库已经合并为单个：

- `lib_ts_watermark/src/main/jniLibs/arm64-v8a/libts-watermark.so`

如果需要重新生成这个单库，使用：

```bash
bash tools/build_single_so_ts_watermark.sh
```

常用验证命令：

```bash
bash gradlew :lib_ts_watermark:testDebugUnitTest :demo:assembleDebug
```
