# legacy_host — dead code, kept only while the app_shell migration lands

This is what `gui/src/host.hh` and `gui/src/os/` were before streamer adopted
[`framework/app_shell`](../../../framework/app_shell) as its platform seam.
**Nothing here is compiled.** It is not referenced by any CMakeLists, and it is
not a fallback — there is exactly one live seam, which is app_shell's.

It exists for one reason: during a seam migration the old implementation is
worth reading beside the new one, and `git show` for four files at once is a
poorer way to do that than having them on disk.

**Delete this whole directory once Wayland, Android and the headless capture are
verified against app_shell.** Git keeps it either way; leaving it around past
that point only invites someone to grep it and believe it.

## What each file became

| here | now |
|---|---|
| `host.hh` | `app_shell/host.hh` + `app_shell/app_view.hh` (the seam split in two by direction of call) |
| `wayland_host.cc` | `app_shell/os/wayland_host.cc` |
| `win32_host.cc` | `app_shell/os/win32_host.cc` |
| `android_host.cc` | `app_shell/os/android_host.cc` + `app_shell/os/activity_bridge.cc` |

The IME half of `android_host.cc` — the UTF-16 conversion, the JNI helpers, the
pending-text slot — was contributed upstream rather than reimplemented, and the
Java it talks to is now `app_shell/platform/android/java/.../AppShellActivity.java`.

## The two differences that made this a migration and not a move

1. **Input.** The old `AppHost::pump(timeout, FrameInput&)` filled a struct the
   frame loop polled. app_shell's `Host::pump(haveWork)` dispatches into
   `AppView` callbacks. `gui/src/streamer_app.*` bridges them through
   `app_shell/frame_input_view.hh`, so every widget still reads a `FrameInput`
   and none of `views.cc` changed.
2. **Who owns the `Renderer`.** It used to live in the host, behind
   `renderer()`/`renderable()`. It belongs to the app now; the host supplies a
   `SurfaceProvider`, and the surface coming and going on Android is reported as
   `onSurfaceLost()`/`onSurfaceRecreated()`.

One thing here was dropped rather than ported: `beep()`. Nothing ever called it.
