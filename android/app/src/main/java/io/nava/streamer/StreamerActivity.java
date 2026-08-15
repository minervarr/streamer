package io.nava.streamer;

import android.app.NativeActivity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.Settings;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;

/**
 * NativeActivity plus the one thing a pure-native app cannot do for itself:
 * text input.
 *
 * <p>NativeActivity hands native code {@code AKEY_EVENT}s, which carry keycodes,
 * not characters. That is enough for Latin typing and useless for everything
 * else — Korean assembles jamo into syllables, Chinese and Japanese pick from
 * candidates over a pinyin/romaji reading. None of that is a sequence of key
 * presses, and reconstructing it natively means reimplementing an input method.
 *
 * <p>So we don't. An off-screen {@link EditText} holds the real buffer and the
 * IME edits it exactly as it would in any Android app; a {@link TextWatcher}
 * mirrors the resulting string down to C++ after every change. Composition,
 * candidate windows, autocorrect, dictation, hardware keyboards and paste all
 * work because Android is doing them, not us.
 *
 * <p>The field is off-screen rather than {@code GONE} or zero-sized: an
 * unfocusable or unlaid-out view is not a valid IME target, and some IMEs
 * refuse to attach to one.
 */
public class StreamerActivity extends NativeActivity {

    // NativeActivity brings this library up with dlopen() from its own native
    // code, which never tells the Java runtime about it. The symbols below are
    // exported and the library is already mapped in the process — but the JVM
    // resolves native methods by searching the libraries *it* was asked to
    // load, so without this every call here dies with UnsatisfiedLinkError.
    // A second load is free: the runtime refcounts and just registers it.
    static {
        System.loadLibrary("streamer_gui");
    }

    /** Mirrors the IME's buffer into the native side. Cursor is in UTF-16 units. */
    private static native void nativeOnTextChanged(String text, int cursor);

    /** The user accepted the text (IME action / Enter). */
    private static native void nativeOnTextCommitted();

    /** The IME went away without the native side asking (back gesture). */
    private static native void nativeOnKeyboardHidden();

    /** System-reserved edges in pixels: notch, status bar, gesture bar, IME. */
    private static native void nativeOnInsets(int left, int top, int right, int bottom);

    private EditText input_;
    private boolean  suppress_;   // guards the echo when native sets the text

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);

        // Keep our own surface from being resized/panned when the IME opens —
        // the GUI draws its own layout and repositions the focused field.
        getWindow().setSoftInputMode(
                WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);

        input_ = new EditText(this);
        input_.setFocusable(true);
        input_.setFocusableInTouchMode(true);
        // Off-screen, not invisible: see the class comment.
        input_.setLayoutParams(new ViewGroup.LayoutParams(1, 1));
        input_.setX(-100.0f);
        input_.setY(-100.0f);
        input_.setGravity(Gravity.TOP);
        input_.setBackgroundColor(0);
        input_.setImeOptions(EditorInfo.IME_ACTION_SEARCH
                | EditorInfo.IME_FLAG_NO_FULLSCREEN);   // no full-screen extract
                                                        // editor in landscape,
                                                        // which would cover the UI
        input_.setSingleLine(true);

        addContentView(input_, input_.getLayoutParams());

        input_.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) {}

            @Override
            public void afterTextChanged(Editable e) {
                if (suppress_) return;
                nativeOnTextChanged(e.toString(), input_.getSelectionEnd());
            }
        });

        input_.setOnEditorActionListener((v, actionId, ev) -> {
            if (actionId == EditorInfo.IME_ACTION_SEARCH
                    || actionId == EditorInfo.IME_ACTION_DONE
                    || actionId == EditorInfo.IME_ACTION_GO) {
                nativeOnTextCommitted();
                return true;
            }
            return false;
        });

        // Back while the IME is up dismisses the IME, not the activity; the
        // native side needs to know its text field lost the keyboard.
        input_.setOnKeyListener((v, keyCode, ev) -> {
            if (keyCode == KeyEvent.KEYCODE_BACK && ev.getAction() == KeyEvent.ACTION_UP) {
                nativeOnKeyboardHidden();
            }
            return false;
        });

        // Insets change on rotation, on a notch coming into play, and every
        // time the IME opens or closes. Pushed rather than polled: the native
        // side would otherwise need a JNI round trip per frame to ask.
        final View root = getWindow().getDecorView();
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                android.graphics.Insets bars = insets.getInsets(
                        WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout()
                                | WindowInsets.Type.ime());
                nativeOnInsets(bars.left, bars.top, bars.right, bars.bottom);
            } else {
                nativeOnInsets(insets.getSystemWindowInsetLeft(),
                        insets.getSystemWindowInsetTop(),
                        insets.getSystemWindowInsetRight(),
                        insets.getSystemWindowInsetBottom());
            }
            return v.onApplyWindowInsets(insets);
        });
    }

    // ---- storage ----------------------------------------------------------

    /**
     * Root of shared storage, e.g. {@code /storage/emulated/0}. The library is
     * ID-addressed on a real filesystem — see the native side — so this has to
     * be a path, not a SAF tree URI.
     */
    @SuppressWarnings("unused")
    public String externalStorageRoot() {
        return Environment.getExternalStorageDirectory().getAbsolutePath();
    }

    /**
     * All-files access. Nothing short of it works here: scoped storage confines
     * an app to its own directory, which is wiped on uninstall — unacceptable
     * for a library that can run to hundreds of gigabytes — and the SAF
     * alternative hands back {@code content://} URIs that {@code
     * std::filesystem} cannot walk.
     *
     * <p>Sends the user to Settings, since this permission cannot be granted
     * from a dialog. Silently does nothing if it is already held.
     */
    @SuppressWarnings("unused")
    public void requestStoragePermission() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return;
        if (Environment.isExternalStorageManager()) return;
        runOnUiThread(() -> {
            try {
                Intent i = new Intent(
                        Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName()));
                startActivity(i);
            } catch (Exception e) {
                // Some OEM builds do not ship the per-app screen; the global
                // list is the documented fallback.
                try {
                    startActivity(new Intent(
                            Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                } catch (Exception ignored) {
                }
            }
        });
    }

    // ---- small platform services the GUI asks for -------------------------

    @SuppressWarnings("unused")
    public void openUrl(final String url) {
        runOnUiThread(() -> {
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
            } catch (Exception ignored) {
            }
        });
    }

    @SuppressWarnings("unused")
    public void setClipboard(final String text) {
        runOnUiThread(() -> {
            ClipboardManager cm =
                    (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
            if (cm != null) cm.setPrimaryClip(ClipData.newPlainText("streamer", text));
        });
    }

    /**
     * Read synchronously, unlike every other method here: the native caller
     * blocks on the result (paste is a user-triggered round trip), and
     * ClipboardManager reads are safe off the UI thread.
     */
    @SuppressWarnings("unused")
    public String getClipboard() {
        ClipboardManager cm = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
        if (cm == null) return "";
        ClipData clip = cm.getPrimaryClip();
        if (clip == null || clip.getItemCount() == 0) return "";
        CharSequence s = clip.getItemAt(0).coerceToText(this);
        return s == null ? "" : s.toString();
    }

    /** The refusal cue. A speaker beep is the wrong idiom on a phone. */
    @SuppressWarnings("unused")
    public void vibrateTick() {
        Vibrator v = (Vibrator) getSystemService(VIBRATOR_SERVICE);
        if (v == null || !v.hasVibrator()) return;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            v.vibrate(VibrationEffect.createOneShot(20, VibrationEffect.DEFAULT_AMPLITUDE));
        } else {
            v.vibrate(20);
        }
    }

    // ---- called FROM native (JNI up-calls; always marshalled to the UI thread,
    // because every View and InputMethodManager method below requires it) ----

    /** Opens the IME, seeding it with the field's current text. */
    @SuppressWarnings("unused")
    public void showKeyboard(final String initialText) {
        runOnUiThread(() -> {
            suppress_ = true;
            input_.setText(initialText == null ? "" : initialText);
            input_.setSelection(input_.getText().length());
            suppress_ = false;

            input_.requestFocus();
            InputMethodManager imm =
                    (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) imm.showSoftInput(input_, InputMethodManager.SHOW_IMPLICIT);
        });
    }

    @SuppressWarnings("unused")
    public void hideKeyboard() {
        runOnUiThread(() -> {
            InputMethodManager imm =
                    (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.hideSoftInputFromWindow(input_.getWindowToken(), 0);
            }
            input_.clearFocus();
        });
    }
}
