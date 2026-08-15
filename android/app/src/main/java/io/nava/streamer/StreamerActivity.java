package io.nava.streamer;

import io.nava.appshell.AppShellActivity;

/**
 * streamer's activity — and there is nothing left in it but the library name.
 *
 * <p>Everything that used to be here (the off-screen {@code EditText}, its
 * {@code TextWatcher}, the insets listener, the clipboard and URL helpers) is
 * {@link AppShellActivity} now, shared with every other app built on app_shell.
 * None of it was ever specific to this one: an IME bridge does not know what a
 * track is.
 *
 * <p>The static block, however, cannot move up. NativeActivity brings the
 * library up with {@code dlopen()} from its own native code, which never tells
 * the Java runtime about it — so despite the symbols being exported and the
 * library already being mapped into this process, the JVM resolves native
 * methods only against libraries it was itself asked to load. Without this,
 * every {@code native} call in the superclass throws {@code
 * UnsatisfiedLinkError} at runtime, with nothing at build time to warn you.
 * A second load costs nothing: the runtime refcounts and simply registers it.
 *
 * <p>Only the consumer knows this name, which is exactly why app_shell cannot
 * do it for us.
 */
public class StreamerActivity extends AppShellActivity {
    static {
        System.loadLibrary("streamer_gui");
    }
}
