package io.github.minicad;

import android.content.Context;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;

// 输入法的连接点（看不见的 1×1 视图）：文本框里的文字由原生界面自己管，这里不保存内容，
// 只把输入法的动作转成三种事件交给原生代码（都在 UI 线程上调用，原生代码转回自己的线程）：
//   - nativeOnComposition：正在组合的文字（拼音等），空串 = 组合取消（没有确认任何文字）
//   - nativeOnCommit：确认的文字；正在组合时它同时结束组合（原生代码先交文字、再结束组合，同 UIContext 的约定）
//   - nativeOnDelete：删除光标前 / 后的字符数（输入法的退格常这样发，而不是发按键）
// 按键（回车、退格等输入法用 sendKeyEvent 发的）照常走窗口的输入队列，原生代码当按键收到
final class MiniCADInputView extends View {
    static native void nativeOnComposition(String text);
    static native void nativeOnCommit(String text);
    static native void nativeOnDelete(int before, int after);

    private String composing = "";

    MiniCADInputView(Context context) {
        super(context);
        setFocusable(false);    // 只在 setActive(true) 时可获得焦点：否则窗口重新获得焦点时系统自动把焦点给它，输入法跟着出来
    }

    // 文本框获得 / 失去焦点（MiniCADActivity.showKeyboard / hideKeyboard）
    void setActive(boolean active) {
        composing = "";
        setFocusable(active);
        setFocusableInTouchMode(active);
        if (active)
            requestFocus();
        else
            clearFocus();
    }

    @Override
    public boolean onCheckIsTextEditor() {
        return true;
    }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo info) {
        info.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
        info.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_ACTION_DONE;
        return new BaseInputConnection(this, false) {
            @Override
            public boolean setComposingText(CharSequence text, int newCursorPosition) {
                composing = text.toString();
                nativeOnComposition(composing);
                return true;
            }

            @Override
            public boolean finishComposingText() {
                // 组合串原样确认（有的输入法切走焦点或按空格时这样做）
                if (!composing.isEmpty()) {
                    String text = composing;
                    composing = "";
                    nativeOnCommit(text);
                }
                return true;
            }

            @Override
            public boolean commitText(CharSequence text, int newCursorPosition) {
                composing = "";
                nativeOnCommit(text.toString());
                return true;
            }

            @Override
            public boolean deleteSurroundingText(int beforeLength, int afterLength) {
                nativeOnDelete(beforeLength, afterLength);
                return true;
            }

            @Override
            public boolean performEditorAction(int actionCode) {
                // 软键盘上的"完成 / 回车"：当回车键
                sendKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_ENTER));
                sendKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER));
                return true;
            }
        };
    }
}
