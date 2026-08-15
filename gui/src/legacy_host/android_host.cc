// Android skin of the GUI (host.hh), on the engine's NativeActivity backend
// (framework/Vk_Canvas_Lb_LAW/platform/android/). Third sibling of
// wayland_host.cc and win32_host.cc.
//
// Three things make this one structurally different from the desktop skins:
//
//   * There is no window at startup. init() returns with no Renderer at all;
//     the surface arrives later as APP_CMD_INIT_WINDOW and can be taken away
//     again at any moment (screen off, app backgrounded, rotation). Hence
//     renderable(), which gui_main.cc checks before touching renderer().
//
//   * Text does not come from key events. The IME owns the buffer through an
//     off-screen EditText in StreamerActivity.java, and reports whole-buffer
//     replacements — see TextEditEvent in core/input.hh for why a stream of
//     characters cannot express composition.
//
//   * Touch is not a mouse. A drag has to scroll rather than press whatever it
//     started on, so the pointer stream is synthesised from gestures below,
//     not passed through.

#include "../host.hh"

#include "android_platform.hh"
#include "jni_util.hh"
#include "keys.hh"

#include "config.hh"
#include "i18n.hh"
#include "service_factory.hh"

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <android_native_app_glue.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

// Defined by gui_main.cc, which is not main() on this platform: NativeActivity
// owns the entry point. Everything below runs before it is called.
int gui_app_main(int argc, char** argv);

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "streamer", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "streamer", __VA_ARGS__)

namespace {

android_app* g_app = nullptr;

// ── the IME's buffer, handed over between threads ───────────────────────────
// afterTextChanged() runs on the Android UI thread; the frame loop runs on the
// glue thread. This is the handoff. Only the newest snapshot matters — an IME
// fires one per keystroke of a composition and each supersedes the last — so
// this is a slot, not a queue.
struct PendingText {
    std::mutex  mu;
    bool        has = false;
    std::string text;
    size_t      cursorByte = 0;
    bool        committed = false;   // user accepted (IME action / Enter)
    bool        dismissed = false;   // IME went away on its own (back gesture)

    // Also from the UI thread: insets change on rotation and whenever the IME
    // opens or closes. Under the same lock rather than a bare struct — four
    // floats written by one thread and read by another is still a data race,
    // and a torn read here would misplace the whole layout for a frame.
    bool  insetsChanged = false;
    float l = 0, t = 0, r = 0, b = 0;
};
PendingText g_pending;

// UTF-16 (what Java hands us) to UTF-8, computing the byte offset of a given
// UTF-16 index along the way.
//
// Deliberately not GetStringUTFChars: that returns *modified* UTF-8, which
// encodes anything outside the BMP as a surrogate pair of three-byte sequences
// rather than one four-byte sequence. Standard UTF-8 decoders — ours included
// — read that as two broken characters. Emoji in an album title would be
// enough to hit it.
std::string utf16_to_utf8(const jchar* u, jsize len, jsize cursorUnits, size_t* cursorByte) {
    std::string out;
    out.reserve((size_t)len * 3 / 2);
    if (cursorByte) *cursorByte = 0;
    for (jsize i = 0; i < len; ) {
        if (cursorByte && i == cursorUnits) *cursorByte = out.size();
        uint32_t cp = u[i++];
        if (cp >= 0xD800 && cp <= 0xDBFF && i < len) {
            uint32_t lo = u[i];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    if (cursorByte && cursorUnits >= len) *cursorByte = out.size();
    return out;
}

// ── JNI helpers on the activity object ──────────────────────────────────────
// Every call here goes to StreamerActivity.java, whose methods marshal to the
// UI thread themselves where Android requires it.

// Calls a no-arg / (String) / ()String method on the activity. Returns false
// if anything went wrong, having cleared the exception (see jni_util.hh: a
// pending exception aborts the process on the *next* JNI call).
bool call_void(const char* name, const char* sig, jvalue* args) {
    if (!g_app) return false;
    JNIEnv* env = vce::platform::jni::env_for(g_app);
    if (!env) return false;
    jobject act = g_app->activity->clazz;
    jclass  cls = env->GetObjectClass(act);
    jmethodID m = env->GetMethodID(cls, name, sig);
    if (!m) {
        vce::platform::jni::check_exc(env, name);
        env->DeleteLocalRef(cls);
        return false;
    }
    env->CallVoidMethodA(act, m, args);
    bool bad = vce::platform::jni::check_exc(env, name);
    env->DeleteLocalRef(cls);
    return !bad;
}

std::string call_string(const char* name) {
    std::string out;
    if (!g_app) return out;
    JNIEnv* env = vce::platform::jni::env_for(g_app);
    if (!env) return out;
    jobject act = g_app->activity->clazz;
    jclass  cls = env->GetObjectClass(act);
    jmethodID m = env->GetMethodID(cls, name, "()Ljava/lang/String;");
    if (!m) {
        vce::platform::jni::check_exc(env, name);
        env->DeleteLocalRef(cls);
        return out;
    }
    jstring s = (jstring)env->CallObjectMethod(act, m);
    if (!vce::platform::jni::check_exc(env, name) && s) {
        const jchar* u = env->GetStringChars(s, nullptr);
        jsize len = env->GetStringLength(s);
        out = utf16_to_utf8(u, len, 0, nullptr);
        env->ReleaseStringChars(s, u);
    }
    if (s) env->DeleteLocalRef(s);
    env->DeleteLocalRef(cls);
    return out;
}

jstring to_jstring(JNIEnv* env, const std::string& utf8) {
    // NewStringUTF also speaks modified UTF-8, with the same supplementary-plane
    // problem as GetStringUTFChars — decode to UTF-16 ourselves.
    std::vector<jchar> u16;
    u16.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size(); ) {
        unsigned char c = (unsigned char)utf8[i];
        uint32_t cp; int n;
        if      (c < 0x80)        { cp = c;        n = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
        else { ++i; continue; }                 // stray continuation byte
        if (i + (size_t)n > utf8.size()) break;
        for (int k = 1; k < n; ++k) cp = (cp << 6) | ((unsigned char)utf8[i + (size_t)k] & 0x3F);
        i += (size_t)n;
        if (cp < 0x10000) {
            u16.push_back((jchar)cp);
        } else {
            cp -= 0x10000;
            u16.push_back((jchar)(0xD800 + (cp >> 10)));
            u16.push_back((jchar)(0xDC00 + (cp & 0x3FF)));
        }
    }
    return env->NewString(u16.data(), (jsize)u16.size());
}

bool call_with_string(const char* name, const std::string& arg) {
    if (!g_app) return false;
    JNIEnv* env = vce::platform::jni::env_for(g_app);
    if (!env) return false;
    jstring js = to_jstring(env, arg);
    jvalue v; v.l = js;
    bool ok = call_void(name, "(Ljava/lang/String;)V", &v);
    env->DeleteLocalRef(js);
    return ok;
}

// ── touch gestures ──────────────────────────────────────────────────────────
// A finger is not a mouse. Pressing and dragging must scroll the list, not
// activate whatever happened to be under the initial touch; a press that never
// moves is a tap and must activate it. So the pointer stream fed to FrameInput
// is synthesised: nothing is emitted on touch-down, and what a release emits
// depends on whether the gesture turned into a drag.
constexpr float kTapSlopPx = 24.0f;   // ~9dp at xhdpi; below this, it's a tap

struct Gesture {
    bool  active = false;
    bool  scrolling = false;          // passed the slop; now a scroll, never a tap
    float startX = 0, startY = 0;
    float lastY = 0;
};
Gesture g_gesture;

class AndroidHost : public gui::AppHost {
public:
    bool init() override
    {
        // No window yet — it arrives as APP_CMD_INIT_WINDOW, possibly after
        // several pump() calls. This cannot fail, and must not: returning
        // false here would abort the app before Android has even shown it.
        assets_ = std::make_unique<AndroidAssetReader>(g_app->activity->assetManager);
        return true;
    }

    AssetReader& assets()   override { return *assets_; }
    Renderer&    renderer() override { return *renderer_; }
    bool renderable()       override { return renderer_ != nullptr; }

    void pump(int timeout_ms, FrameInput& input) override
    {
        sink_ = &input;

        // With no surface there is nothing to draw and no reason to spin:
        // block until the system has something to say. gui_main.cc calls this
        // in a tight loop while !renderable(), so without this the process
        // would burn a core in the background.
        int wait = renderer_ ? timeout_ms : -1;

        int events = 0;
        android_poll_source* source = nullptr;
        while (ALooper_pollOnce(wait, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(g_app, source);
            if (g_app->destroyRequested) { quit_ = true; break; }
            wait = 0;   // drain whatever else is queued without sleeping again
        }

        drain_pending_text(input);

        if (resized_) {
            resized_ = false;
            if (renderer_) renderer_->notifyResized();
            dirty_ = true;
        }
        sink_ = nullptr;
    }

    bool quit_requested() override { return quit_; }
    bool take_dirty() override { bool d = dirty_; dirty_ = false; return d; }

    void beep() override
    {
        // A speaker beep is the wrong idiom on a phone and needs an audio
        // stream we do not otherwise open; the platform's refusal cue is a
        // short haptic tick, which the activity does in one call.
        call_void("vibrateTick", "()V", nullptr);
    }

    bool open_url(const std::string& url) override
    {
        // Same guard as the desktop skins: only ever hand a plain http(s) URL
        // to the system. Intent.ACTION_VIEW on an arbitrary scheme can reach
        // any app on the device.
        if (url.find("://") == std::string::npos) return false;
        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return false;
        return call_with_string("openUrl", url);
    }

    void pick_directory(std::function<void(const std::string&)> cb) override
    {
        // Same as the Wayland skin: no native picker. The settings screen's
        // path field is the way to change this, and the default already points
        // somewhere usable (see android_main), so nothing is blocked on it.
        //
        // Note it must stay *synchronous*: gui_main.cc captures its callback by
        // reference, so a callback that outlived the frame would dangle.
        cb("");
    }

    void set_clipboard_text(const std::string& utf8) override
    {
        call_with_string("setClipboard", utf8);
    }

    std::string get_clipboard_text() override { return call_string("getClipboard"); }

    Insets safe_area() override
    {
        // Refreshed by the activity on every layout/insets change; reading it
        // here is a plain field read, not a JNI round trip per frame.
        return insets_;
    }

    void show_keyboard(const std::string& text, size_t cursor_byte) override
    {
        (void)cursor_byte;   // the field seeds its caret at the end
        call_with_string("showKeyboard", text);
    }

    void hide_keyboard() override { call_void("hideKeyboard", "()V", nullptr); }

    // ── glue callbacks ─────────────────────────────────────────────────────
    static void on_cmd(android_app* app, int32_t cmd)
    {
        auto* self = static_cast<AndroidHost*>(app->userData);
        if (!self) return;
        switch (cmd) {
            case APP_CMD_INIT_WINDOW:
                self->create_surface();
                break;
            case APP_CMD_TERM_WINDOW:
                self->destroy_surface();
                break;
            case APP_CMD_WINDOW_RESIZED:
            case APP_CMD_CONFIG_CHANGED:
                self->resized_ = true;
                break;
            case APP_CMD_GAINED_FOCUS:
                self->dirty_ = true;
                break;
            default:
                break;
        }
    }

    static int32_t on_input(android_app* app, AInputEvent* ev)
    {
        auto* self = static_cast<AndroidHost*>(app->userData);
        if (!self || !self->sink_) return 0;
        int32_t type = AInputEvent_getType(ev);
        if (type == AINPUT_EVENT_TYPE_MOTION) return self->on_motion(ev);
        if (type == AINPUT_EVENT_TYPE_KEY)    return self->on_key(ev);
        return 0;
    }

private:
    void create_surface()
    {
        if (!g_app->window) return;
        provider_ = std::make_unique<AndroidSurfaceProvider>(g_app->window);
        // 3 images, matching the desktop skins: the swapchain present mode is
        // the renderer's business, and triple buffering is right on a phone too.
        renderer_ = std::make_unique<Renderer>(*provider_, *assets_, 3);
        dirty_ = true;
        LOGI("surface created (%dx%d)", renderer_->width(), renderer_->height());
    }

    void destroy_surface()
    {
        // Order matters: the Renderer holds the VkSurfaceKHR the provider made
        // from the window, so it must go first. gui_main.cc has already been
        // told to stop drawing by renderable() going false.
        renderer_.reset();
        provider_.reset();
        LOGI("surface destroyed");
    }

    void drain_pending_text(FrameInput& input)
    {
        std::string text; size_t cursor = 0;
        bool has = false, committed = false, dismissed = false;
        {
            std::lock_guard<std::mutex> lock(g_pending.mu);
            has = g_pending.has;
            if (has) { text = std::move(g_pending.text); cursor = g_pending.cursorByte; }
            committed = g_pending.committed;
            dismissed = g_pending.dismissed;
            g_pending.has = false;
            g_pending.committed = false;
            g_pending.dismissed = false;
            if (g_pending.insetsChanged) {
                insets_ = {g_pending.l, g_pending.t, g_pending.r, g_pending.b};
                g_pending.insetsChanged = false;
                dirty_ = true;
            }
        }
        if (has) {
            TextEditEvent e;
            e.text = std::move(text);
            e.cursorByte = cursor;
            input.onTextEdit(e);
            dirty_ = true;
        }
        // The IME's action key is the search screen's submit, and the widgets
        // already treat Enter as that — so report it as one rather than adding
        // a second path through the UI.
        if (committed) { KeyEvent k; k.keyCode = key::Enter; k.down = true; input.onKey(k); dirty_ = true; }
        if (dismissed) { KeyEvent k; k.keyCode = key::Escape; k.down = true; input.onKey(k); dirty_ = true; }
    }

    int32_t on_motion(AInputEvent* ev)
    {
        int32_t action = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
        float x = AMotionEvent_getX(ev, 0);
        float y = AMotionEvent_getY(ev, 0);

        switch (action) {
            case AMOTION_EVENT_ACTION_DOWN: {
                g_gesture = {true, false, x, y, y};
                // Deliberately no pointer-down yet: whether this is a press or
                // the start of a scroll is not knowable until the finger moves.
                PointerEvent p{PointerAction::Move, x, y, 0};
                sink_->onPointer(p);
                dirty_ = true;
                return 1;
            }
            case AMOTION_EVENT_ACTION_MOVE: {
                if (!g_gesture.active) return 1;
                if (!g_gesture.scrolling &&
                    std::hypot(x - g_gesture.startX, y - g_gesture.startY) > kTapSlopPx) {
                    g_gesture.scrolling = true;
                }
                if (g_gesture.scrolling) {
                    // Natural direction: content follows the finger. wheelDelta
                    // is positive for "away from the user", i.e. an upward
                    // swipe scrolls down, so the sign is inverted here.
                    WheelEvent w; w.x = x; w.y = y;
                    w.deltaY = (y - g_gesture.lastY) / 40.0f;
                    sink_->onWheel(w);
                    g_gesture.lastY = y;
                    dirty_ = true;
                }
                PointerEvent p{PointerAction::Move, x, y, 0};
                sink_->onPointer(p);
                return 1;
            }
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL: {
                bool tap = g_gesture.active && !g_gesture.scrolling &&
                           action == AMOTION_EVENT_ACTION_UP;
                g_gesture.active = false;
                if (tap) {
                    // Both edges in the same frame. The widgets are
                    // immediate-mode and read pointerWentDown/Up off one
                    // FrameInput, so a tap is exactly that pair.
                    PointerEvent d{PointerAction::Down, x, y, 0};
                    PointerEvent u{PointerAction::Up,   x, y, 0};
                    sink_->onPointer(d);
                    sink_->onPointer(u);
                    dirty_ = true;
                }
                return 1;
            }
            default:
                return 0;
        }
    }

    int32_t on_key(AInputEvent* ev)
    {
        int32_t code = AKeyEvent_getKeyCode(ev);
        bool down = AKeyEvent_getAction(ev) == AKEY_EVENT_ACTION_DOWN;

        int mapped = 0;
        switch (code) {
            case AKEYCODE_DEL:         mapped = key::Backspace; break;
            case AKEYCODE_FORWARD_DEL: mapped = key::Delete;    break;
            case AKEYCODE_ENTER:
            case AKEYCODE_NUMPAD_ENTER: mapped = key::Enter;    break;
            case AKEYCODE_TAB:         mapped = key::Tab;       break;
            case AKEYCODE_DPAD_LEFT:   mapped = key::Left;      break;
            case AKEYCODE_DPAD_RIGHT:  mapped = key::Right;     break;
            case AKEYCODE_DPAD_UP:     mapped = key::Up;        break;
            case AKEYCODE_DPAD_DOWN:   mapped = key::Down;      break;
            case AKEYCODE_MOVE_HOME:   mapped = key::Home;      break;
            case AKEYCODE_MOVE_END:    mapped = key::End;       break;
            case AKEYCODE_PAGE_UP:     mapped = key::PageUp;    break;
            case AKEYCODE_PAGE_DOWN:   mapped = key::PageDown;  break;
            case AKEYCODE_ESCAPE:      mapped = key::Escape;    break;
            case AKEYCODE_BACK:
                // Let the system handle Back: returning 0 lets it close the
                // activity, which is what a user expects from the gesture.
                return 0;
            default:
                // Everything else is text, and text does not come from here —
                // the IME owns it (see the file header). Swallowing unknown
                // keys would break the hardware-keyboard case; passing them on
                // lets Android route them to the focused EditText, which is
                // exactly where they should go.
                return 0;
        }
        KeyEvent k; k.keyCode = mapped; k.down = down;
        sink_->onKey(k);
        dirty_ = true;
        return 1;
    }

    std::unique_ptr<AndroidAssetReader>    assets_;
    std::unique_ptr<AndroidSurfaceProvider> provider_;
    std::unique_ptr<Renderer>               renderer_;
    FrameInput* sink_ = nullptr;

    Insets insets_;
    bool quit_    = false;
    bool dirty_   = false;
    bool resized_ = false;
};

} // namespace

namespace gui {
std::unique_ptr<AppHost> make_host()
{
    auto h = std::make_unique<AndroidHost>();
    g_app->userData    = h.get();
    g_app->onAppCmd    = &AndroidHost::on_cmd;
    g_app->onInputEvent = &AndroidHost::on_input;
    return h;
}
} // namespace gui

// ── JNI down-calls from StreamerActivity.java ───────────────────────────────
// All of these run on the Android UI thread. They only ever touch g_pending
// under its lock and then wake the looper; nothing else here is thread-safe.

extern "C" {

JNIEXPORT void JNICALL
Java_io_nava_streamer_StreamerActivity_nativeOnTextChanged(JNIEnv* env, jclass,
                                                           jstring text, jint cursor)
{
    const jchar* u = env->GetStringChars(text, nullptr);
    jsize len = env->GetStringLength(text);
    size_t cursorByte = 0;
    std::string utf8 = utf16_to_utf8(u, len, cursor, &cursorByte);
    env->ReleaseStringChars(text, u);

    {
        std::lock_guard<std::mutex> lock(g_pending.mu);
        g_pending.has = true;
        g_pending.text = std::move(utf8);
        g_pending.cursorByte = cursorByte;
    }
    if (g_app) ALooper_wake(g_app->looper);
}

JNIEXPORT void JNICALL
Java_io_nava_streamer_StreamerActivity_nativeOnTextCommitted(JNIEnv*, jclass)
{
    { std::lock_guard<std::mutex> lock(g_pending.mu); g_pending.committed = true; }
    if (g_app) ALooper_wake(g_app->looper);
}

JNIEXPORT void JNICALL
Java_io_nava_streamer_StreamerActivity_nativeOnKeyboardHidden(JNIEnv*, jclass)
{
    { std::lock_guard<std::mutex> lock(g_pending.mu); g_pending.dismissed = true; }
    if (g_app) ALooper_wake(g_app->looper);
}

JNIEXPORT void JNICALL
Java_io_nava_streamer_StreamerActivity_nativeOnInsets(JNIEnv*, jclass,
                                                      jint l, jint t, jint r, jint b)
{
    {
        std::lock_guard<std::mutex> lock(g_pending.mu);
        g_pending.insetsChanged = true;
        g_pending.l = (float)l; g_pending.t = (float)t;
        g_pending.r = (float)r; g_pending.b = (float)b;
    }
    if (g_app) ALooper_wake(g_app->looper);
}

} // extern "C"

// ── entry point ─────────────────────────────────────────────────────────────

namespace {

// Copies an APK asset out to a real file. libcurl's CURLOPT_CAINFO wants a
// path on a filesystem, and an asset inside the APK is not one — it is an
// offset into a zip. Done once per launch; the file is small (~190 KB) and
// rewriting it is how a bundle update actually reaches the app.
bool extract_asset(AAssetManager* mgr, const char* name, const std::filesystem::path& dest)
{
    AAsset* a = AAssetManager_open(mgr, name, AASSET_MODE_STREAMING);
    if (!a) { LOGE("asset missing: %s", name); return false; }
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    FILE* f = std::fopen(dest.c_str(), "wb");
    if (!f) { AAsset_close(a); LOGE("cannot write %s", dest.c_str()); return false; }
    char buf[16384];
    int n;
    while ((n = AAsset_read(a, buf, sizeof buf)) > 0) std::fwrite(buf, 1, (size_t)n, f);
    std::fclose(f);
    AAsset_close(a);
    return true;
}

} // namespace

// stdout/stderr on Android go to /dev/null. Everything this program says when
// something goes wrong — a 401 from the API, a failed track, the account
// pool's report with the repair command — is written there, so on this
// platform it was all being discarded, silently, exactly when it mattered.
// Pump both through a pipe into logcat instead.
static int g_log_pipe[2] = {-1, -1};

static void* log_reader(void*)
{
    char buf[512];
    ssize_t n;
    while ((n = read(g_log_pipe[0], buf, sizeof(buf) - 1)) > 0) {
        if (buf[n - 1] == '\n') --n;
        buf[n] = '\0';
        __android_log_write(ANDROID_LOG_INFO, "streamer.out", buf);
    }
    return nullptr;
}

static void redirect_stdio_to_logcat()
{
    setvbuf(stdout, nullptr, _IOLBF, 0);   // line-buffered, so lines arrive
    setvbuf(stderr, nullptr, _IONBF, 0);   // unbuffered: errors arrive at once
    if (pipe(g_log_pipe) != 0) return;
    dup2(g_log_pipe[1], STDOUT_FILENO);
    dup2(g_log_pipe[1], STDERR_FILENO);
    pthread_t t;
    if (pthread_create(&t, nullptr, log_reader, nullptr) == 0)
        pthread_detach(t);
}

extern "C" void android_main(android_app* app)
{
    g_app = app;
    redirect_stdio_to_logcat();

    // Everything below must happen before gui_app_main() reads a single file:
    // on this platform none of it can come from the environment.
    const std::filesystem::path internal = app->activity->internalDataPath;

    // Where the library goes. Real filesystem paths, not SAF — the catalog,
    // the backup and the downloader all walk the tree with std::filesystem,
    // and a content:// URI is not a path. Requires MANAGE_EXTERNAL_STORAGE,
    // which the activity asks for.
    std::string external = call_string("externalStorageRoot");
    if (external.empty()) external = "/storage/emulated/0";
    const std::filesystem::path downloads = std::filesystem::path(external) / "Music" / "streamer";

    config::set_platform_dirs(internal, downloads);

    // No system trust store is readable by an app, and libcurl here is built
    // with CURL_CA_BUNDLE=none, so without this every HTTPS request fails.
    const std::filesystem::path ca = internal / "cacert.pem";
    if (extract_asset(app->activity->assetManager, "cacert.pem", ca))
        qobuz::set_ca_bundle_path(ca.string());
    else
        LOGE("no CA bundle — every HTTPS request will fail verification");

    // getenv("LANG") does not exist here; AConfiguration does, without JNI.
    // Goes in through the same door as an explicit config.toml `language`.
    char lang[3] = {0, 0, 0};
    if (app->config) AConfiguration_getLanguage(app->config, lang);
    i18n::init(std::string(lang, std::strlen(lang)));

    call_void("requestStoragePermission", "()V", nullptr);

    LOGI("config=%s downloads=%s lang=%s", internal.c_str(), downloads.c_str(), lang);

    gui_app_main(0, nullptr);

    // Returning from android_main ends the process; the glue expects the
    // activity to be finished by then.
    ANativeActivity_finish(app->activity);
}
