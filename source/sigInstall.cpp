#include <switch.h>
#include <filesystem>
#include "util/error.hpp"
#include "ui/MainApplication.hpp"
#include "util/curl.hpp"
#include "util/util.hpp"
#include "util/unzip.hpp"
#include "util/config.hpp"
#include "util/lang.hpp"

namespace inst::ui {
    extern MainApplication *mainApp;
}

namespace sig {
    static bool installComponent(const std::string& displayName, const std::string& version, const std::string& url, const std::string& zipPath) {
        inst::ui::instPage::setInstBarPerc(0);
        inst::ui::instPage::setInstInfoText("sig.downloading"_lang + displayName + " " + version);
        if (!inst::curl::downloadFile(url, zipPath.c_str(), 0, true)) {
            std::filesystem::remove(zipPath);
            return false;
        }
        inst::ui::instPage::setInstInfoText("sig.extracting"_lang + displayName);
        const bool ok = inst::zip::extractFile(zipPath, "sdmc:/");
        std::filesystem::remove(zipPath);
        return ok;
    }

    void installSigPatches () {
        bpcInitialize();
        try {
            if (inst::util::getIPAddress() == "1.0.0.127") {
                inst::ui::mainApp->CreateShowDialog("main.net.title"_lang, "main.net.desc"_lang, {"common.ok"_lang}, true);
                bpcExit();
                return;
            }

            auto ultrahandInfo = inst::util::fetchLatestRelease(inst::config::ultrahandUrl);
            auto sysPatchInfo = inst::util::fetchLatestRelease(inst::config::sysPatchUrl);
            auto emuiiboInfo = inst::util::fetchLatestRelease(inst::config::emuiiboUrl);

            if (ultrahandInfo.empty() || sysPatchInfo.empty() || emuiiboInfo.empty()) {
                inst::ui::mainApp->CreateShowDialog("sig.fetch_failed"_lang, "sig.fetch_failed_desc"_lang, {"common.ok"_lang}, true);
                bpcExit();
                return;
            }

            const std::string desc = "sig.confirm_desc"_lang + std::string("\n\n") +
                                     "Ultrahand Overlay: " + ultrahandInfo[0] + "\n" +
                                     "sys-patch: " + sysPatchInfo[0] + "\n" +
                                     "emuiibo: " + emuiiboInfo[0];
            const int rc = inst::ui::mainApp->CreateShowDialog("sig.confirm_title"_lang, desc, {"sig.install"_lang, "common.cancel"_lang}, false);
            if (rc != 0) {
                bpcExit();
                return;
            }

            inst::ui::instPage::loadInstallScreen();
            inst::ui::instPage::setTopInstInfoText("sig.top_info"_lang);

            if (!installComponent("Ultrahand Overlay", ultrahandInfo[0], ultrahandInfo[1], inst::config::appDir + "/ultrahand.zip")) {
                inst::ui::mainApp->CreateShowDialog("sig.download_failed"_lang, "Ultrahand Overlay", {"common.ok"_lang}, true);
                inst::ui::instPage::loadMainMenu();
                bpcExit();
                return;
            }
            if (!installComponent("sys-patch", sysPatchInfo[0], sysPatchInfo[1], inst::config::appDir + "/sys-patch.zip")) {
                inst::ui::mainApp->CreateShowDialog("sig.download_failed"_lang, "sys-patch", {"common.ok"_lang}, true);
                inst::ui::instPage::loadMainMenu();
                bpcExit();
                return;
            }
            if (!installComponent("emuiibo", emuiiboInfo[0], emuiiboInfo[1], inst::config::appDir + "/emuiibo.zip")) {
                inst::ui::mainApp->CreateShowDialog("sig.download_failed"_lang, "emuiibo", {"common.ok"_lang}, true);
                inst::ui::instPage::loadMainMenu();
                bpcExit();
                return;
            }

            if (inst::ui::mainApp->CreateShowDialog("sig.install_complete"_lang, "sig.complete_desc"_lang, {"sig.restart"_lang, "sig.later"_lang}, false) == 0) {
                bpcRebootSystem();
            }
            inst::ui::instPage::loadMainMenu();
        }
        catch (std::exception& e) {
            LOG_DEBUG("Failed to install Ultrahand Overlay and sys-patch");
            LOG_DEBUG("%s", e.what());
            inst::ui::mainApp->CreateShowDialog("sig.generic_error"_lang, (std::string)e.what(), {"common.ok"_lang}, true);
            inst::ui::instPage::loadMainMenu();
        }
        bpcExit();
    }
}
