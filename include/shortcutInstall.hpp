#pragma once

namespace shortcut {
    // Call early in main(): when running from the HOME Menu forwarder, switch
    // libnx to the application exit handshake so quitting doesn't crash.
    void configureHomeExit();

    // Settings → "Add Leaf Installer to HOME Menu": confirms, installs the
    // forwarder from romfs:/shortcut/template.bin and reports the result.
    void startShortcutInstall();
}
