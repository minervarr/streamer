// Android's entry point.
//
// app_shell owns main() and WinMain() on the desktops, but not this one:
// NativeActivity starts android_main() and hands it the android_app* that only
// this function ever sees — which is why AndroidHost is INJECTED here rather
// than manufactured by make_host() (see streamer_app.hh's g_injected_host).
//
// app_shell's own docs say this file should be about six lines. It is longer,
// and every extra line is streamer's rather than the shell's: on this platform
// nothing can come from the environment, so where the library goes, which
// certificates exist and what language to speak all have to be settled before
// the app reads its first file. That is application knowledge, and it belongs
// on this side of the seam.

#include "streamer_app.hh"

#include "android_host.hh"     // app_shell
#include "activity_bridge.hh"  // app_shell: external_storage_root()

#include "config.hh"
#include "i18n.hh"
#include "service_factory.hh"

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "streamer", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "streamer", __VA_ARGS__)

namespace {

// Copies a packaged asset out of the APK onto the filesystem.
//
// Needed because some consumers of a file want a PATH, not a stream: libcurl
// takes a CA bundle by filename and has no way to be handed bytes. Assets
// inside an APK are not files — they live compressed inside the archive — so
// the only way to give curl a path is to write one.
bool extract_asset(AAssetManager* mgr, const char* name,
                   const std::filesystem::path& dest) {
    if (!mgr) return false;
    std::error_code ec;
    if (std::filesystem::exists(dest, ec)) return true;   // already unpacked

    AAsset* a = AAssetManager_open(mgr, name, AASSET_MODE_BUFFER);
    if (!a) return false;
    const void* buf = AAsset_getBuffer(a);
    const off_t  len = AAsset_getLength(a);
    bool ok = false;
    if (buf && len > 0) {
        if (FILE* f = std::fopen(dest.c_str(), "wb")) {
            ok = std::fwrite(buf, 1, (size_t)len, f) == (size_t)len;
            std::fclose(f);
        }
    }
    AAsset_close(a);
    return ok;
}

} // namespace

extern "C" void android_main(android_app* app) {
    // FIRST, before anything can print: the host's constructor installs the
    // stdout/stderr redirect into logcat. Without it every diagnostic this
    // process produces goes to /dev/null — which is how an HTTP 401 and a
    // read-only download directory both stayed invisible for a whole session.
    AndroidHost host(app);

    const std::filesystem::path internal = app->activity->internalDataPath;

    // Where the library goes. A real filesystem path, never SAF: the catalog,
    // the backup and the downloader all walk the tree with std::filesystem,
    // and a content:// URI is not a path. Needs MANAGE_EXTERNAL_STORAGE, which
    // app_shell asks for once (os/storage_permission.cc).
    std::string external = activity::external_storage_root();
    if (external.empty()) external = "/storage/emulated/0";
    const std::filesystem::path downloads =
        std::filesystem::path(external) / "Music" / "streamer";
    config::set_platform_dirs(internal, downloads);

    // No system trust store is readable by an app here, and libcurl is built
    // with CURL_CA_BUNDLE=none, so without this EVERY https request fails
    // verification — login, search and download alike.
    const std::filesystem::path ca = internal / "cacert.pem";
    if (extract_asset(app->activity->assetManager, "cacert.pem", ca))
        qobuz::set_ca_bundle_path(ca.string());
    else
        LOGE("no CA bundle -- every HTTPS request will fail verification");

    // getenv("LANG") does not exist on Android; AConfiguration does, and needs
    // no JNI. It goes in through the same door an explicit config.toml
    // `language` would, so the setting still wins.
    char lang[3] = {0, 0, 0};
    if (app->config) AConfiguration_getLanguage(app->config, lang);
    i18n::init(std::string(lang, std::strlen(lang)));

    LOGI("config=%s downloads=%s lang=%s", internal.c_str(), downloads.c_str(), lang);

    // The host exists but has not been initialised; app_shell_main() does
    // that, because the order (init, then create the Renderer, then loop) is
    // identical on all three platforms and is written down exactly once.
    g_injected_host = &host;

    // argc = 0: an app launched by an Intent has no command line. What it was
    // launched WITH, if anything, arrives through Host::launchArgument().
    app_shell_main(0, nullptr);

    // Returning from android_main ends the process, and the glue expects the
    // activity to have been finished by then.
    ANativeActivity_finish(app->activity);
}
