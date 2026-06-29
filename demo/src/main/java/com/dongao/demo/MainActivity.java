package com.dongao.demo;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import com.dongao.lib.tswatermark.EncryptedTsWatermarker;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

public class MainActivity extends Activity {

    private TextView tvPath, tvProgress;
    private Button btnSelect, btnWatermark;
    private EditText etCryptokey;
    private EditText etIv;
    private String videoPath;
    private String watermarkPath;
    private String outputPath;

    private final Handler handler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(40, 80, 40, 40);

        btnSelect = new Button(this);
        btnSelect.setText("Select Encrypted TS");
        layout.addView(btnSelect);

        tvPath = new TextView(this);
        tvPath.setText("No file selected");
        tvPath.setPadding(0, 20, 0, 20);
        layout.addView(tvPath);

        etCryptokey = new EditText(this);
        etCryptokey.setHint("AES-128 key (16 chars or 32 hex chars)");
        etCryptokey.setSingleLine(true);
        etCryptokey.setPadding(0, 10, 0, 10);
        etCryptokey.setInputType(android.text.InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD);
        etCryptokey.setText("1075514326b26649");
        layout.addView(etCryptokey);

        etIv = new EditText(this);
        etIv.setHint("IV (16 chars or 32 hex chars)");
        etIv.setSingleLine(true);
        etIv.setPadding(0, 10, 0, 10);
        etIv.setText("30783632396531646432353566363135");
        layout.addView(etIv);


        btnWatermark = new Button(this);
        btnWatermark.setText("Add Watermark");
        btnWatermark.setEnabled(false);
        layout.addView(btnWatermark);

        tvProgress = new TextView(this);
        tvProgress.setPadding(0, 20, 0, 0);
        layout.addView(tvProgress);

        setContentView(layout);

        btnSelect.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, 1);
        });

        btnWatermark.setOnClickListener(v -> startWatermark());
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode == RESULT_OK && data != null && data.getData() != null) {
            Uri uri = data.getData();
            // Copy to cache for FFmpeg to access
            videoPath = copyToCache(uri);
            if (videoPath != null) {
                tvPath.setText("Selected: " + new File(videoPath).getName());
                btnWatermark.setEnabled(true);
            }
        }
    }

    private String copyToCache(Uri uri) {
        try {
            String name = "input_video";
            android.database.Cursor cursor = getContentResolver().query(uri, null, null, null, null);
            if (cursor != null && cursor.moveToFirst()) {
                int idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (idx >= 0) name = cursor.getString(idx);
                cursor.close();
            }
            File out = new File(getCacheDir(), name);
            InputStream in = getContentResolver().openInputStream(uri);
            FileOutputStream fos = new FileOutputStream(out);
            byte[] buf = new byte[8192];
            int len;
            while ((len = in.read(buf)) > 0) fos.write(buf, 0, len);
            in.close();
            fos.close();
            return out.getAbsolutePath();
        } catch (Exception e) {
            e.printStackTrace();
            return null;
        }
    }



    private void startWatermark() {
        watermarkPath = generateTextWatermark("FFmpeg");
        String outName = new File(videoPath).getName();
        String baseName = outName.contains(".") ? outName.substring(0, outName.lastIndexOf(".")) : outName;
        String outBase = getCacheDir().getAbsolutePath() + File.separator + "watermarked_" + baseName;

        String keyStr = etCryptokey.getText().toString().trim();
        String hexIv = etIv.getText().toString().trim();
        if (keyStr.isEmpty() || hexIv.isEmpty()) {
            tvProgress.setText("Key and IV are required for encrypted TS.");
            return;
        }

        outputPath = outBase + "_encrypted.ts";
        tvProgress.setText("Processing...");
        final String fOut = outputPath;
        new Thread(() -> {
            try {
                EncryptedTsWatermarker.watermarkSegment(videoPath, watermarkPath, fOut, keyStr, hexIv);
                handler.post(() -> {
                    tvProgress.setText("Done!\n" + fOut);
                    Toast.makeText(MainActivity.this, "Watermark success!", Toast.LENGTH_LONG).show();
                });
            } catch (Exception e) {
                handler.post(() -> tvProgress.setText("Failed: " + e.getMessage()));
            }
        }).start();
    }

    private String generateTextWatermark(String text) {
        Bitmap bitmap = Bitmap.createBitmap(256, 64, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        Paint paint = new Paint();
        paint.setColor(Color.argb(180, 255, 255, 255));
        paint.setTextSize(32);
        paint.setAntiAlias(true);
        canvas.drawText(text, 10, 40, paint);
        String path = getCacheDir().getAbsolutePath() + File.separator + "watermark.png";
        try {
            FileOutputStream fos = new FileOutputStream(path);
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, fos);
            fos.close();
        } catch (Exception e) { e.printStackTrace(); }
        bitmap.recycle();
        return path;
    }
}
