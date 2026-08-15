# legacy_host — dead code, kept only while the app_shell migration lands

This is what `gui/src/host.hh` and `gui/src/os/` were before streamer adopted
[`framework/app_shell`](../../../framework/app_shell) as its platform seam.
**Nothing here is compiled.** It is not referenced by any CMakeLists, and it is
not a fallback — there is exactly one live seam, which is app_shell's.

It exists for one reason: during a seam migration the old implementation is
worth reading beside the new one, and `git show` for four files at once is a
poorer way to do that than having them on disk.

## Status: waiting on Windows, and only on Windows

| | verified against app_shell |
|---|---|
| Wayland | yes — builds, runs, `--selftest` 129 assertions |
| Android | yes — on a moto g06: IME, cutout, keyboard inset, a real download |
| headless capture | yes — drives the real app now, three window shapes |
| **Win32** | **no — not even compiled** |

Windows is the reason this directory still exists. There is no MinGW and no MSVC
on the machine this migration was done on, so `app_shell/os/win32_host.cc` and
the clipboard / `openUrl` / folder-picker code written into it have never been
through a compiler, let alone run. `win32_host.cc` here is the version that
demonstrably worked, and it is worth having beside the new one the first time
somebody builds on Windows.

**Delete this whole directory the moment a Windows build runs.** Git keeps it
either way, and leaving it past that point only invites someone to grep it and
believe it.

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
