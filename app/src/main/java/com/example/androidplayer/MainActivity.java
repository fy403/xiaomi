package com.example.androidplayer;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.provider.DocumentsContract;
import android.provider.MediaStore;
import android.provider.Settings;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.webkit.MimeTypeMap;
import android.widget.Button;
import android.widget.EditText;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import com.example.androidplayer.databinding.ActivityMainBinding;

import java.io.File;

public class MainActivity extends AppCompatActivity {
    // Used to load the 'androidplayer' library on application startup.
    static {
        System.loadLibrary("androidplayer");
    }

    private Player player;
    private Handler mHandler;
    private SeekBar mSeekBar;

    private static final int SELECT_VIDEO_REQUEST_CODE = 1;
    private static final int SELECT_OUTPUT_DIRECTORY_REQUEST_CODE = 2;
    private EditText etOutputPath;
    private TextView tvStatus;
    private TextView tvSelectedOutputDirectory;
    private String selectedVideoPath;
    private Uri selectedOutputDirectoryUri;
    private String selectedOutputDirectoryPath;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        }

        setContentView(ActivityMainBinding.inflate(getLayoutInflater()).getRoot());

        mSeekBar = findViewById(R.id.seekBar);

        mHandler = new Handler(Looper.getMainLooper()) {
            public void handleMessage(@NonNull Message msg) {
                super.handleMessage(msg);
                if (msg.what == 1) {
                    Bundle bundle = msg.getData();
                    int progress = bundle.getInt("progress");
                    mSeekBar.setProgress(progress);
                }
            }
        };

        player = new Player();
        player.setDataSource("file:/sdcard/Movies/movie.mp4");

        ((SurfaceView) findViewById(R.id.surfaceView)).getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(@NonNull SurfaceHolder holder) {
                player.setSurface(holder.getSurface());
            }

            @Override
            public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height) {

            }

            @Override
            public void surfaceDestroyed(@NonNull SurfaceHolder holder) {

            }
        });

        Thread progressThread = new Thread(() -> {
            int progress;
            while (true) {
                progress = (int) Math.round(player.getProgress() * 100);
                setSeekBar(progress);
                try {
                    Thread.sleep(500);
                } catch (InterruptedException e) {
                    throw new RuntimeException(e);
                }
            }
        });
        Button play = findViewById(R.id.button);
        play.setText("播放");
        play.setOnClickListener(v -> {
            switch (player.getState()) {
                case None:
                case End:
                    player.start();
                    if (!progressThread.isAlive())
                        progressThread.start();
                    play.setText("暂停");
                    break;
                case Playing:
                    player.pause(true);
                    play.setText("播放");
                    break;
                case Paused:
                    player.pause(false);
                    play.setText("暂停");
                    break;
                default:
                    break;
            }
        });

        Button stop = findViewById(R.id.button2);
        stop.setOnClickListener(v -> {
            player.stop();
            play.setText("播放");
            setSeekBar(0);
        });

        Button speed = findViewById(R.id.button3);
        speed.setText("1x");
        speed.setOnClickListener(v -> {
            switch (speed.getText().toString()) {
                case "2x":
                    player.setSpeed(3);
                    speed.setText("3x");
                    break;
                case "3x":
                    player.setSpeed(0.5f);
                    speed.setText("0.5x");
                    break;
                case "0.5x":
                    player.setSpeed(1);
                    speed.setText("1x");
                    break;
                case "1x":
                    player.setSpeed(2);
                    speed.setText("2x");
                    break;
            }
        });

        mSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
//                if (fromUser)
//                    player.seek((double) progress / 100);
            }
            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
                player.pause(true);
            }
            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                // 只在停止拖动时处理
                int progress = seekBar.getProgress();
                // 跳转到指定位置
                player.seek((double) progress / 100);
                // 恢复播放（如果需要）
                player.pause(false);
            }
        });

        // 请求存储权限
        requestStoragePermission();
    }

    private void setSeekBar(int progress) {
        Bundle bundle = new Bundle();
        bundle.putInt("progress", progress);
        Message msg = new Message();
        msg.setData(bundle);
        msg.what = 1;
        mHandler.sendMessage(msg);
    }

    // 请求存储权限
    private void requestStoragePermission() {
        if (checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED ||
                checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{
                    android.Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE
            }, 1);
        }
    }
}