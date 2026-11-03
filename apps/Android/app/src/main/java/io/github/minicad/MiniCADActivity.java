package io.github.minicad;

import android.app.NativeActivity;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.inputmethod.InputMethodManager;
import android.widget.Toast;
import android.window.OnBackInvokedDispatcher;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.HashMap;

// 原生代码（libMiniCADAndroid.so，入口 android_main）由 NativeActivity 按清单里的 android.app.lib_name 加载。
// 这里只放原生代码做不了的事，由 AndroidApp 经 JNI 调用（可以在任意线程上调用，界面操作都转到 UI 线程）：
//   - showKeyboard / hideKeyboard：软键盘与输入法。NativeActivity 本身没有 InputConnection，
//     所以加一个看不见的 MiniCADInputView 接收输入法的组合串与确认的文字，经 native 方法交给原生代码
//   - 返回手势（Android 13 起的 OnBackInvokedCallback）：全屏时先退出全屏，否则交给原生代码当 Esc 处理，不直接结束 Activity
//   - setFullScreen：全屏（隐藏状态栏和导航栏）
//   - 窗口铺满整个屏幕（targetSdk 35 起系统强制如此），状态栏、手势条、挖孔、软键盘占的边经 nativeOnInsets 交给原生代码，界面让开
//   - pickFiles：系统的文件选择器（ACTION_OPEN_DOCUMENT），选中的 content:// 复制到 cache/open/ 后把路径交回原生代码
//   - saveFile：保存后把私有目录里的文件交给用户——从 content:// 打开的写回原处，否则用 ACTION_CREATE_DOCUMENT 让用户选位置
//   - openUrl：浏览器打开网址
//   - httpGet：检查更新时取 GitHub Release 的信息（原生代码在后台线程上调用）
public class MiniCADActivity extends NativeActivity {
    private static final String TAG = "MiniCAD";
    private static final int REQUEST_OPEN = 1001;
    private static final int REQUEST_SAVE = 1002;

    private MiniCADInputView inputView;
    private boolean fullScreen;     // UI 线程
    private int pendingOpenId;
    private String pendingSavePath;
    private String pendingSavedText, pendingFailedText, pendingOpenFailedText;
    private final HashMap<String, Uri> openedUris = new HashMap<>();    // cache/open/ 里的副本 → 原来的地址

    // NativeActivity 自己 dlopen 原生库，不经类加载器登记，Java 的 native 方法就找不到实现；
    // 这里再按名字加载一次（同一个库，只是登记），名字与清单的 android.app.lib_name 相同
    static {
        System.loadLibrary("MiniCADAndroid");
    }

    // 选好的文件（paths 为空数组 = 取消）；在后台线程上调用，原生代码自己转回界面线程
    private static native void nativeOnFilesPicked(int requestId, String[] paths);
    // 返回手势 / 返回键（UI 线程）
    static native void nativeOnBack();
    // 被系统占用的边（像素，UI 线程）
    static native void nativeOnInsets(int left, int top, int right, int bottom);
    // 返回手势退出了全屏（UI 线程）
    static native void nativeOnFullScreenExited();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // NativeActivity 把窗口的绘图表面和输入队列交给了原生代码；输入法要一个有焦点的视图才能连接，
        // 加一个 1×1 的透明视图放在内容之上
        inputView = new MiniCADInputView(this);
        addContentView(inputView, new ViewGroup.LayoutParams(1, 1));
        if (Build.VERSION.SDK_INT >= 30) {
            // Android 11 起统一按铺满处理（15 起系统也强制），软键盘不改窗口尺寸，只报告它占的高度；
            // Android 10 由系统让开状态栏，adjustResize 缩小窗口
            getWindow().setDecorFitsSystemWindows(false);
            getWindow().getDecorView().setOnApplyWindowInsetsListener((v, insets) -> {
                android.graphics.Insets i = insets.getInsets(WindowInsets.Type.systemBars()
                        | WindowInsets.Type.displayCutout() | WindowInsets.Type.ime());
                nativeOnInsets(i.left, i.top, i.right, i.bottom);
                return v.onApplyWindowInsets(insets);
            });
        }
        if (Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::onBackGesture);
        }
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        onBackGesture();    // Android 12 及以下
    }

    // 全屏时先退出全屏，否则交给原生代码当 Esc
    private void onBackGesture() {
        if (fullScreen) {
            applyFullScreen(false);
            nativeOnFullScreenExited();
        } else {
            nativeOnBack();
        }
    }

    public void setFullScreen(final boolean on) {
        runOnUiThread(() -> applyFullScreen(on));
    }

    // 隐藏状态栏和导航栏；从屏幕边缘滑动临时显示，不改变窗口的边距
    @SuppressWarnings("deprecation")
    private void applyFullScreen(boolean on) {
        fullScreen = on;
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController c = getWindow().getInsetsController();
            if (c == null)
                return;
            if (on) {
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                c.hide(WindowInsets.Type.systemBars());
            } else {
                c.show(WindowInsets.Type.systemBars());
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(on
                    ? View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                      | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                      | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    : 0);
        }
    }

    public void showKeyboard() {
        runOnUiThread(() -> {
            inputView.setActive(true);
            InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            imm.restartInput(inputView);
            imm.showSoftInput(inputView, InputMethodManager.SHOW_IMPLICIT);
        });
    }

    public void hideKeyboard() {
        runOnUiThread(() -> {
            InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            imm.hideSoftInputFromWindow(inputView.getWindowToken(), 0);
            inputView.setActive(false);
        });
    }

    // failedText：读不了时的提示（原生代码按界面语言翻译好，{0} 换成文件名）
    public void pickFiles(final int requestId, final boolean multiple, final String failedText) {
        runOnUiThread(() -> {
            pendingOpenId = requestId;
            pendingOpenFailedText = failedText;
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, multiple);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            try {
                startActivityForResult(intent, REQUEST_OPEN);
            } catch (ActivityNotFoundException e) {
                Log.e(TAG, "no document picker", e);
                nativeOnFilesPicked(requestId, new String[0]);
            }
        });
    }

    // savedText / failedText：提示文字（原生代码按界面语言翻译好，后面接文件名）
    public void saveFile(final String path, final String displayName, final String savedText, final String failedText) {
        runOnUiThread(() -> {
            Uri original;
            synchronized (openedUris) {
                original = openedUris.get(path);
            }
            if (original != null) {
                copyInBackground(path, original, savedText, failedText);
                return;
            }
            pendingSavePath = path;
            pendingSavedText = savedText;
            pendingFailedText = failedText;
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("application/octet-stream");
            intent.putExtra(Intent.EXTRA_TITLE, displayName);
            try {
                startActivityForResult(intent, REQUEST_SAVE);
            } catch (ActivityNotFoundException e) {
                Log.e(TAG, "no document creator", e);
            }
        });
    }

    public void openUrl(final String url) {
        runOnUiThread(() -> {
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
            } catch (ActivityNotFoundException e) {
                Log.e(TAG, "no browser", e);
            }
        });
    }

    // HTTPS GET，阻塞；HTTP 200 时返回响应内容，否则 null。返回字节而不是 String：JNI 的 UTF 接口是改过的 UTF-8，表情符号会变样
    public byte[] httpGet(String url) {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL(url).openConnection();
            c.setConnectTimeout(15000);
            c.setReadTimeout(15000);
            c.setRequestProperty("Accept", "application/vnd.github+json");
            if (c.getResponseCode() != HttpURLConnection.HTTP_OK)
                return null;
            try (InputStream in = c.getInputStream()) {
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                byte[] buf = new byte[16384];
                for (int n; (n = in.read(buf)) > 0; )
                    out.write(buf, 0, n);
                return out.toByteArray();
            }
        } catch (IOException e) {
            Log.w(TAG, "httpGet failed: " + url, e);
            return null;
        } finally {
            if (c != null)
                c.disconnect();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_OPEN) {
            final int id = pendingOpenId;
            final String failedText = pendingOpenFailedText;
            final ArrayList<Uri> uris = new ArrayList<>();
            if (resultCode == RESULT_OK && data != null) {
                ClipData clip = data.getClipData();
                if (clip != null) {
                    for (int i = 0; i < clip.getItemCount(); ++i)
                        uris.add(clip.getItemAt(i).getUri());
                } else if (data.getData() != null) {
                    uris.add(data.getData());
                }
            }
            new Thread(() -> nativeOnFilesPicked(id, copyToCache(uris, failedText))).start();
        } else if (requestCode == REQUEST_SAVE) {
            final String path = pendingSavePath;
            pendingSavePath = null;
            if (resultCode == RESULT_OK && data != null && data.getData() != null && path != null) {
                synchronized (openedUris) {
                    openedUris.put(path, data.getData());   // 之后再保存直接写到这里
                }
                copyInBackground(path, data.getData(), pendingSavedText, pendingFailedText);
            }
        }
    }

    private String[] copyToCache(ArrayList<Uri> uris, String failedText) {
        File dir = new File(getCacheDir(), "open");
        dir.mkdirs();
        ArrayList<String> paths = new ArrayList<>();
        ArrayList<String> failed = new ArrayList<>();
        for (Uri uri : uris) {
            try {
                getContentResolver().takePersistableUriPermission(uri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            } catch (SecurityException e) {
                // 只读的来源（例如某些云盘）：照样能打开，保存时另选位置
            }
            final String name = displayName(uri);
            File out = new File(dir, name);
            try (InputStream in = openForRead(uri);
                 OutputStream os = new FileOutputStream(out)) {
                copy(in, os);
                paths.add(out.getAbsolutePath());
                synchronized (openedUris) {
                    openedUris.put(out.getAbsolutePath(), uri);
                }
            } catch (Exception e) {
                Log.e(TAG, "copy failed: " + uri, e);
                failed.add(name);
            }
        }
        if (!failed.isEmpty() && failedText != null) {
            final String text = failedText.replace("{0}", String.join(", ", failed));
            runOnUiThread(() -> Toast.makeText(this, text, Toast.LENGTH_LONG).show());
        }
        return paths.toArray(new String[0]);
    }

    // 三星 Android 13 的"下载"（com.android.providers.downloads.documents 的 raw: 文档）只读打开时转到媒体库
    // 去掉照片的位置信息，被媒体库以 SecurityException 拒绝；读写模式直接打开文件，不经媒体库
    private InputStream openForRead(Uri uri) throws java.io.IOException {
        try {
            InputStream in = getContentResolver().openInputStream(uri);
            if (in != null)
                return in;
        } catch (SecurityException e) {
            Log.w(TAG, "read-only open refused, retrying read-write: " + uri);
        }
        android.os.ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "rw");
        if (pfd == null)
            throw new java.io.FileNotFoundException(uri.toString());
        return new android.os.ParcelFileDescriptor.AutoCloseInputStream(pfd);
    }

    private void copyInBackground(final String path, final Uri uri, final String savedText, final String failedText) {
        new Thread(() -> {
            boolean ok = false;
            try (InputStream in = new FileInputStream(path);
                 OutputStream os = getContentResolver().openOutputStream(uri, "wt")) {
                copy(in, os);
                ok = true;
            } catch (Exception e) {
                Log.e(TAG, "save failed: " + uri, e);
            }
            final boolean saved = ok;
            final String name = displayName(uri);
            runOnUiThread(() -> Toast.makeText(this, (saved ? savedText : failedText) + name, Toast.LENGTH_SHORT).show());
        }).start();
    }

    private String displayName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0))
                return c.getString(0).replace('/', '_');
        } catch (Exception e) {
            // 拿不到名字时用地址的最后一段
        }
        String last = uri.getLastPathSegment();
        return last != null ? last.replace('/', '_').replace(':', '_') : "drawing";
    }

    private static void copy(InputStream in, OutputStream out) throws java.io.IOException {
        byte[] buf = new byte[64 * 1024];
        for (int n; (n = in.read(buf)) > 0; )
            out.write(buf, 0, n);
    }
}
