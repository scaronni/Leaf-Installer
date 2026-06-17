#include <switch.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include "util/error.hpp"
#include "ui/MainApplication.hpp"
#include "ui/cfwInstPage.hpp"
#include "util/curl.hpp"
#include "util/util.hpp"
#include "util/unzip.hpp"
#include "util/config.hpp"
#include "util/lang.hpp"
#include "cfwInstall.hpp"

namespace inst::ui {
    extern MainApplication *mainApp;
}

namespace cfw {
    // Staging directory: extraction targets here while HOS is running, then the
    // leaf-updater payload moves everything to the SD root on next boot. This
    // avoids fighting Atmosphère's fs-mitm locks on /atmosphere/package3 etc.
    static const std::string kStagingDir = "sdmc:/leaf-offline-update";
    static const std::string kStagingDirSlash = kStagingDir + "/";
    static const std::string kPayloadPath = "sdmc:/bootloader/payloads/leaf-updater.bin";
    static const std::string kHekateIniPath = "sdmc:/bootloader/hekate_ipl.ini";
    static const std::string kAutobootPrev = kStagingDirSlash + ".autoboot-prev";
    static const std::string kLeafUpdateSection = "Leaf Update";

    // Recursively moves the contents of `src` into `dst`, merging into existing
    // directories and overwriting clashing files. Removes `src` when done.
    static void mergeMoveDir(const std::filesystem::path& src, const std::filesystem::path& dst) {
        if (!std::filesystem::exists(src)) return;
        std::filesystem::create_directories(dst);
        for (const auto& entry : std::filesystem::directory_iterator(src)) {
            const auto target = dst / entry.path().filename();
            if (entry.is_directory()) {
                mergeMoveDir(entry.path(), target);
                std::error_code ec;
                std::filesystem::remove(entry.path(), ec);
            } else {
                std::error_code ec;
                std::filesystem::remove(target, ec);
                std::filesystem::rename(entry.path(), target);
            }
        }
        std::error_code ec;
        std::filesystem::remove(src, ec);
    }

    // hekate's release ships hekate_ctcaer_X.Y.Z.bin at the zip root. Picofly
    // (and any other modchip that boots /payload.bin) needs that file to land
    // at sdmc:/payload.bin. So inside the staging dir, rename it to payload.bin
    // — the payload then moves staging/payload.bin to sdmc:/payload.bin.
    static void renameHekatePayloadInStaging() {
        for (const auto& entry : std::filesystem::directory_iterator(kStagingDir)) {
            if (!entry.is_regular_file()) continue;
            const std::string name = entry.path().filename().string();
            if (name.rfind("hekate_ctcaer_", 0) != 0) continue;
            if (name.size() < 4 || name.compare(name.size() - 4, 4, ".bin") != 0) continue;
            const std::filesystem::path target = kStagingDirSlash + "payload.bin";
            std::error_code ec;
            std::filesystem::remove(target, ec);
            std::filesystem::rename(entry.path(), target);
            return;
        }
    }

    static void applyPostProcess(const std::string& tag) {
        // emuiibo's release zip stages everything under a top-level "SdOut/"
        // folder; flatten it into the staging dir so the final layout matches
        // the SD root.
        if (tag == "emuiibo_sdout") {
            mergeMoveDir(kStagingDirSlash + "SdOut", kStagingDir);
        } else if (tag == "hekate_payload") {
            renameHekatePayloadInStaging();
        }
    }

    // Reads hekate_ipl.ini and walks it line-by-line. Returns the 1-based
    // index of the section header `[Leaf Update]` (counting non-config
    // entries, the same way hekate's `autoboot=N` indexes work), the parsed
    // value of the current `autoboot=` line, or -1 for either if not found.
    struct IniScanResult {
        int leafUpdateIndex = -1; // 1-based index, -1 if section missing
        int currentAutoboot = -1; // -1 if missing
    };

    static IniScanResult scanHekateIni(const std::string& iniText) {
        IniScanResult out;
        std::istringstream stream(iniText);
        std::string line;
        int entryIndex = 0;
        while (std::getline(stream, line)) {
            // Strip trailing CR for files written with \r\n.
            if (!line.empty() && line.back() == '\r') line.pop_back();

            // Skip leading whitespace for the keyword checks.
            size_t i = 0;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
            if (i >= line.size()) continue;

            if (line[i] == '[') {
                // Section header. Hekate counts every non-[config] section
                // as a numbered entry starting from 1.
                const auto close = line.find(']', i + 1);
                if (close == std::string::npos) continue;
                const std::string name = line.substr(i + 1, close - i - 1);
                if (name == "config") continue;
                entryIndex++;
                if (name == kLeafUpdateSection) out.leafUpdateIndex = entryIndex;
            } else if (line.compare(i, 9, "autoboot=") == 0) {
                int v = 0;
                bool any = false;
                for (size_t j = i + 9; j < line.size(); j++) {
                    char c = line[j];
                    if (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); any = true; }
                    else if (any) break;
                    else if (c == ' ' || c == '\t') continue;
                    else break;
                }
                if (any) out.currentAutoboot = v;
            }
        }
        return out;
    }

    // Rewrites the first `autoboot=` line in the [config] section to point at
    // `newValue`. Preserves the rest of the ini (comments, ordering, other
    // sections). Returns the new file contents or an empty string on
    // failure.
    static std::string rewriteAutoboot(const std::string& iniText, int newValue) {
        std::ostringstream out;
        std::istringstream stream(iniText);
        std::string line;
        bool rewritten = false;
        while (std::getline(stream, line)) {
            std::string emit = line;
            const bool hadCR = !emit.empty() && emit.back() == '\r';
            if (hadCR) emit.pop_back();
            if (!rewritten) {
                size_t i = 0;
                while (i < emit.size() && (emit[i] == ' ' || emit[i] == '\t')) i++;
                if (i < emit.size() && emit.compare(i, 9, "autoboot=") == 0) {
                    emit = emit.substr(0, i) + "autoboot=" + std::to_string(newValue);
                    rewritten = true;
                }
            }
            out << emit;
            if (hadCR) out << '\r';
            out << '\n';
        }
        return rewritten ? out.str() : std::string();
    }

    // Returns 0 if armed, 1 if user declined the reboot prompt, negative on
    // hard failure (ini missing, [Leaf Update] missing, write failure).
    static int armHekateAutoboot() {
        std::ifstream in(kHekateIniPath);
        if (!in.good()) return -1;
        std::stringstream buf;
        buf << in.rdbuf();
        const std::string iniText = buf.str();
        in.close();

        const auto scan = scanHekateIni(iniText);
        if (scan.leafUpdateIndex < 0) return -2;
        if (scan.currentAutoboot < 0) return -3;

        // Snapshot the previous value so the payload can put it back.
        {
            std::ofstream prev(kAutobootPrev);
            if (!prev.good()) return -4;
            prev << scan.currentAutoboot;
        }

        const std::string updated = rewriteAutoboot(iniText, scan.leafUpdateIndex);
        if (updated.empty()) return -5;

        std::ofstream w(kHekateIniPath, std::ios::binary | std::ios::trunc);
        if (!w.good()) return -6;
        w.write(updated.data(), updated.size());
        if (!w.good()) return -7;
        return 0;
    }

    enum class InstallResult { Ok, DownloadFailed, ExtractFailed };

    static InstallResult installComponent(const inst::ui::CfwComponent& c, const std::string& zipPath, std::string& errorOut) {
        errorOut.clear();
        inst::ui::instPage::setInstBarPerc(0);
        inst::ui::instPage::setInstInfoText("cfw.downloading"_lang + c.displayName + " " + c.version);
        if (!inst::curl::downloadFile(c.url, zipPath.c_str(), 0, true)) {
            std::filesystem::remove(zipPath);
            return InstallResult::DownloadFailed;
        }
        inst::ui::instPage::setInstInfoText("cfw.extracting"_lang + c.displayName);
        const bool ok = inst::zip::extractFile(zipPath, kStagingDirSlash, &errorOut);
        std::filesystem::remove(zipPath);
        if (!ok) return InstallResult::ExtractFailed;
        applyPostProcess(c.postProcess);

        // After the main zip is staged, pull any sibling raw assets the
        // component asked for (e.g. Atmosphère's `fusee.bin`) and drop them
        // into the staging tree at the requested relative path so the
        // payload's recursive move puts them in place.
        for (const auto& extra : c.extraAssets) {
            const std::string& assetUrl  = extra.first;
            const std::string& destPath  = extra.second;
            if (assetUrl.empty() || destPath.empty()) continue;
            const std::string fullDest = kStagingDirSlash + destPath;
            inst::ui::instPage::setInstInfoText("cfw.downloading"_lang + c.displayName + " (" + destPath + ")");
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(fullDest).parent_path(), ec);
            if (!inst::curl::downloadFile(assetUrl, fullDest.c_str(), 0, true)) {
                // Don't fail the whole component install for a missing extra
                // — log via the diagnostic string but let the main contents
                // still apply on next boot.
                LOG_DEBUG("Extra asset download failed: %s\n", destPath.c_str());
            }
        }

        return InstallResult::Ok;
    }

    void startCfwInstall() {
        try {
            if (inst::util::getIPAddress() == "1.0.0.127") {
                inst::ui::mainApp->CreateShowDialog("main.net.title"_lang, "main.net.desc"_lang, {"common.ok"_lang}, true);
                return;
            }

            // We can't apply the staged update without leaf-updater.bin sitting
            // in hekate's payloads dir; bail loudly if the user is missing it
            // (typical cause: they updated Leaf-Installer.nro by hand instead
            // of unpacking the release zip).
            if (!std::filesystem::exists(kPayloadPath)) {
                inst::ui::mainApp->CreateShowDialog("cfw.missing_payload_title"_lang, "cfw.missing_payload_desc"_lang, {"common.ok"_lang}, true);
                return;
            }

            const int rc = inst::ui::mainApp->CreateShowDialog("cfw.confirm_title"_lang, "cfw.confirm_desc"_lang, {"cfw.install"_lang, "common.cancel"_lang}, false);
            if (rc != 0) {
                return;
            }

            auto atmosphereInfo = inst::util::fetchLatestRelease(inst::config::atmosphereUrl);
            auto hekateInfo     = inst::util::fetchLatestRelease(inst::config::hekateUrl, "Nyx");
            auto ultrahandInfo  = inst::util::fetchLatestRelease(inst::config::ultrahandUrl, "sdout");
            auto sysPatchInfo   = inst::util::fetchLatestRelease(inst::config::sysPatchUrl);
            auto emuiiboInfo    = inst::util::fetchLatestRelease(inst::config::emuiiboUrl);

            if (atmosphereInfo.empty() || hekateInfo.empty() || ultrahandInfo.empty() || sysPatchInfo.empty() || emuiiboInfo.empty()) {
                inst::ui::mainApp->CreateShowDialog("cfw.fetch_failed"_lang, "cfw.fetch_failed_desc"_lang, {"common.ok"_lang}, true);
                return;
            }

            // Atmosphère's release ships `fusee.bin` as a standalone asset
            // next to the main .zip; we need it at
            // sdmc:/bootloader/payloads/fusee.bin (where hekate_ipl.ini's
            // [Atmosphere] entry chainloads it from) alongside the rest of
            // the extracted tree. Resolve its URL up front; empty means the
            // release didn't include it this round (rare) and we'll just
            // skip it without failing the install.
            const std::string fuseeUrl = inst::util::fetchReleaseAssetUrl(inst::config::atmosphereUrl, "fusee.bin");
            std::vector<std::pair<std::string, std::string>> atmosphereExtras;
            if (!fuseeUrl.empty()) {
                atmosphereExtras.push_back({fuseeUrl, "bootloader/payloads/fusee.bin"});
            }

            std::vector<inst::ui::CfwComponent> components = {
                {"Atmosphère",        atmosphereInfo[0], atmosphereInfo[1], "atmosphere.zip", "",               "", true, atmosphereExtras},
                {"hekate - Nyx",      hekateInfo[0],     hekateInfo[1],     "hekate.zip",     "hekate_payload", ""},
                {"Ultrahand Overlay", ultrahandInfo[0],  ultrahandInfo[1],  "ultrahand.zip",  "",               ""},
                {"sys-patch",         sysPatchInfo[0],   sysPatchInfo[1],   "sys-patch.zip",  "",               ""},
                {"emuiibo",           emuiiboInfo[0],    emuiiboInfo[1],    "emuiibo.zip",    "emuiibo_sdout",  ""},
            };

            for (auto& c : components) {
                auto it = inst::config::cfwInstalled.find(c.displayName);
                if (it != inst::config::cfwInstalled.end()) c.installedVersion = it->second;
            }

            inst::ui::mainApp->cfwinstpage->setComponents(components);
            inst::ui::mainApp->LoadLayout(inst::ui::mainApp->cfwinstpage);
        }
        catch (std::exception& e) {
            LOG_DEBUG("Failed to fetch release information");
            LOG_DEBUG("%s", e.what());
            inst::ui::mainApp->CreateShowDialog("cfw.generic_error"_lang, (std::string)e.what(), {"common.ok"_lang}, true);
        }
    }

    void installSelectedComponents(const std::vector<inst::ui::CfwComponent>& components) {
        if (components.empty()) return;
        try {
            inst::ui::instPage::loadInstallScreen();
            inst::ui::instPage::setTopInstInfoText("cfw.top_info"_lang);

            // Wipe any prior staging tree so this run's contents don't mingle
            // with leftovers from a previously-aborted attempt.
            {
                std::error_code ec;
                std::filesystem::remove_all(kStagingDir, ec);
                std::filesystem::create_directories(kStagingDir, ec);
            }

            std::vector<std::string> succeeded;
            std::vector<std::string> failedSummary;
            for (const auto& c : components) {
                std::string errorDetail;
                const auto res = installComponent(c, inst::config::appDir + "/" + c.zipName, errorDetail);
                if (res != InstallResult::Ok) {
                    const std::string title = (res == InstallResult::ExtractFailed)
                        ? "cfw.extract_failed"_lang
                        : "cfw.download_failed"_lang;
                    std::string body = c.displayName;
                    if (!errorDetail.empty()) body += "\n\n" + errorDetail;
                    inst::ui::mainApp->CreateShowDialog(title, body, {"common.ok"_lang}, true);
                    failedSummary.push_back(c.displayName);
                    continue;
                }
                inst::config::cfwInstalled[c.displayName] = c.version;
                inst::config::setConfig();
                succeeded.push_back(c.displayName);
            }

            if (succeeded.empty()) {
                inst::ui::instPage::loadMainMenu();
                return;
            }

            // Arm hekate to boot the leaf-updater payload on the next reboot.
            // If anything goes wrong (ini missing, [Leaf Update] entry missing,
            // write failed) we still tell the user the staging succeeded, but
            // skip the auto-reboot — they can apply manually from hekate's
            // payload menu.
            const int armRc = armHekateAutoboot();

            std::string completeBody = "cfw.complete_desc"_lang;
            if (!failedSummary.empty()) {
                completeBody += "\n\n" + "cfw.skipped_prefix"_lang;
                for (size_t i = 0; i < failedSummary.size(); i++) {
                    if (i) completeBody += ", ";
                    completeBody += failedSummary[i];
                }
            }
            if (armRc != 0) {
                completeBody += "\n\n" + "cfw.arm_failed"_lang;
            }

            const std::vector<std::string> buttons = (armRc == 0)
                ? std::vector<std::string>{"cfw.restart"_lang, "cfw.later"_lang}
                : std::vector<std::string>{"common.ok"_lang};
            const int choice = inst::ui::mainApp->CreateShowDialog("cfw.install_complete"_lang, completeBody, buttons, false);
            if (armRc == 0 && choice == 0) {
                bpcInitialize();
                bpcRebootSystem();
                bpcExit();
            }
            inst::ui::instPage::loadMainMenu();
        }
        catch (std::exception& e) {
            LOG_DEBUG("Failed to install custom firmware components");
            LOG_DEBUG("%s", e.what());
            inst::ui::mainApp->CreateShowDialog("cfw.generic_error"_lang, (std::string)e.what(), {"common.ok"_lang}, true);
            inst::ui::instPage::loadMainMenu();
        }
    }
}
