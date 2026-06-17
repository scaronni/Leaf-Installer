#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <switch.h>

#include "amiiboGen.hpp"
#include "ui/MainApplication.hpp"
#include "util/config.hpp"
#include "util/curl.hpp"
#include "util/error.hpp"
#include "util/json.hpp"
#include "util/lang.hpp"
#include "util/util.hpp"

// The stb image headers are header-only; the implementation macros must be
// defined in exactly one translation unit. This file is that unit.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "util/stb_image.h"
#include "util/stb_image_resize.h"
#include "util/stb_image_write.h"

namespace inst::ui {
    extern MainApplication *mainApp;
}

namespace amiibo {
    static const std::string AMIIBO_DB_PATH = "sdmc:/emuiibo/amiibos.json";
    static const std::string AMIIBO_DIR     = "sdmc:/emuiibo/amiibo/";

    static bool isBlacklistedCharacter(char c) {
        if (!(c >= 0 && c < 128)) return true;
        switch (c) {
            case '!': case '?': case '.': case ',': case '\'': case '\\':
                return true;
            default:
                return false;
        }
    }

    static int randByte() {
        return std::rand() & 0xFF;
    }

    static uint16_t swap_uint16(uint16_t val) {
        return (val << 8) | (val >> 8);
    }

    // Resize the just-downloaded amiibo PNG to a 150px-tall image with the
    // original aspect ratio, converting RGB to RGBA when needed. Matches the
    // behavior of the original AmiiboGenerator tool so emuiibo gets the same
    // file layout it expects.
    static void resizeAmiiboImage(const std::string& imagePath) {
        int width = 0, height = 0, channels = 0;
        unsigned char* data = stbi_load(imagePath.c_str(), &width, &height, &channels, 0);
        if (data == nullptr) return;

        if (height <= 0 || width <= 0) {
            stbi_image_free(data);
            return;
        }

        const int newHeight = 150;
        const int newWidth = newHeight * width / height;
        if (newWidth <= 0) {
            stbi_image_free(data);
            return;
        }

        unsigned char* resized = (unsigned char*)std::malloc(newWidth * newHeight * channels);
        if (resized == nullptr) {
            stbi_image_free(data);
            return;
        }
        stbir_resize_uint8(data, width, height, 0, resized, newWidth, newHeight, 0, channels);

        if (channels == 3) {
            unsigned char* rgba = (unsigned char*)std::malloc(newWidth * newHeight * 4);
            if (rgba != nullptr) {
                for (int i = 0; i < newWidth * newHeight; i++) {
                    rgba[i * 4 + 0] = resized[i * 3 + 0];
                    rgba[i * 4 + 1] = resized[i * 3 + 1];
                    rgba[i * 4 + 2] = resized[i * 3 + 2];
                    rgba[i * 4 + 3] = 255;
                }
                std::free(resized);
                resized = rgba;
                channels = 4;
            }
        }

        stbi_write_png(imagePath.c_str(), newWidth, newHeight, channels, resized, newWidth * channels);

        std::free(resized);
        stbi_image_free(data);
    }

    // Outcome of processing a single amiibo entry.
    enum class GenResult { Created, ImageBackfilled, Skipped };

    // True if the amiibo's icon is already on disk and non-empty.
    static bool amiiboImagePresent(const std::string& dir) {
        std::error_code ec;
        const std::string imagePath = dir + "amiibo.png";
        return std::filesystem::exists(imagePath, ec) &&
               std::filesystem::file_size(imagePath, ec) > 0;
    }

    // Downloads the amiibo icon into `dir` (as amiibo.png) and resizes it.
    // Returns true if a file was fetched. resizeAmiiboImage leaves the
    // original download in place if it can't decode/resize, so a successful
    // download always yields a valid PNG.
    static bool downloadAmiiboImage(const nlohmann::json& entry, const std::string& dir) {
        const std::string imageUrl = entry.value("image", "");
        if (imageUrl.empty()) return false;
        const std::string imagePath = dir + "amiibo.png";
        if (!inst::curl::downloadFile(imageUrl, imagePath.c_str(), 0, false)) return false;
        resizeAmiiboImage(imagePath);
        return true;
    }

    // Build the on-disk path for a given amiibo. emuiibo's layout requires
    // <series>/<name>_<full_id>/ — sanitize series/name the same way the
    // original AmiiboGenerator does (strip non-ASCII + a few punctuation chars,
    // map '/' to '_').
    static std::string amiiboDir(const nlohmann::json& entry, const std::string& fullId) {
        std::string series = entry["amiiboSeries"].get<std::string>();
        std::string name = entry["name"].get<std::string>();
        series.erase(std::remove_if(series.begin(), series.end(), &isBlacklistedCharacter), series.end());
        name.erase(std::remove_if(name.begin(), name.end(), &isBlacklistedCharacter), name.end());
        std::replace(series.begin(), series.end(), '/', '_');
        std::replace(name.begin(), name.end(), '/', '_');
        return AMIIBO_DIR + series + "/" + name + "_" + fullId + "/";
    }

    // Generates a fresh amiibo dir, backfills a missing icon into an existing
    // one, or skips an entry that's already complete (or malformed).
    static GenResult generateOne(const nlohmann::json& entry) {
        const std::string head = entry.value("head", "");
        const std::string tail = entry.value("tail", "");
        const std::string fullId = head + tail;
        if (fullId.length() < 16) return GenResult::Skipped;

        const std::string dir = amiiboDir(entry, fullId);
        if (std::filesystem::exists(dir)) {
            // Already generated. Backfill just the icon if it's missing — e.g.
            // the folder was created by an older build that predated image
            // support or whose image download failed. Leave amiibo.json /
            // amiibo.flag untouched: they're correct and the UUID inside may
            // have been used/customized already.
            if (!amiiboImagePresent(dir) && downloadAmiiboImage(entry, dir))
                return GenResult::ImageBackfilled;
            return GenResult::Skipped;
        }

        const std::string gameIdStr  = fullId.substr(0, 4);
        const std::string variantStr = fullId.substr(4, 2);
        const std::string figureStr  = fullId.substr(6, 2);
        const std::string modelStr   = fullId.substr(8, 4);
        const std::string seriesStr  = fullId.substr(12, 2);

        const int gameIdBe  = (int)std::strtol(gameIdStr.c_str(),  nullptr, 16);
        const int gameIdInt = swap_uint16(gameIdBe);
        const int variant   = (int)std::strtol(variantStr.c_str(), nullptr, 16);
        const int figure    = (int)std::strtol(figureStr.c_str(),  nullptr, 16);
        const int model     = (int)std::strtol(modelStr.c_str(),   nullptr, 16);
        const int series    = (int)std::strtol(seriesStr.c_str(),  nullptr, 16);

        const time_t now = std::time(nullptr);
        struct tm* t = std::gmtime(&now);
        const int day = t->tm_mday;
        const int month = t->tm_mon + 1;
        const int year = t->tm_year + 1900;

        nlohmann::json out = nlohmann::json::object();
        out["name"] = entry["name"];
        out["write_counter"] = 0;
        out["version"] = 0;
        out["first_write_date"] = {{"y", year}, {"m", month}, {"d", day}};
        out["last_write_date"]  = {{"y", year}, {"m", month}, {"d", day}};
        out["mii_charinfo_file"] = "mii-charinfo.bin";
        out["id"] = {
            {"game_character_id", gameIdInt},
            {"character_variant", variant},
            {"figure_type",       figure},
            {"series",            series},
            {"model_number",      model}
        };
        out["uuid"] = nlohmann::json::array();
        for (int i = 0; i < 7; i++) out["uuid"].push_back(randByte());
        out["uuid"].push_back(0);
        out["uuid"].push_back(0);
        out["uuid"].push_back(0);

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) return GenResult::Skipped;

        std::ofstream(dir + "amiibo.flag").close();

        std::ofstream meta(dir + "amiibo.json");
        meta << out.dump(2);
        meta.close();

        downloadAmiiboImage(entry, dir);
        return GenResult::Created;
    }

    void generateAmiiboData() {
        try {
            if (inst::util::getIPAddress() == "1.0.0.127") {
                inst::ui::mainApp->CreateShowDialog("main.net.title"_lang, "main.net.desc"_lang, {"common.ok"_lang}, true);
                return;
            }

            const std::string desc = "amiibo.confirm_desc"_lang + std::string("\n\n") + inst::config::amiiboApiUrl;
            const int rc = inst::ui::mainApp->CreateShowDialog(
                "amiibo.confirm_title"_lang,
                desc,
                {"amiibo.generate"_lang, "common.cancel"_lang},
                false);
            if (rc != 0) return;

            inst::ui::instPage::loadInstallScreen();
            inst::ui::instPage::setTopInstInfoText("amiibo.top_info"_lang);
            inst::ui::instPage::setInstInfoText("amiibo.downloading_db"_lang);
            inst::ui::instPage::setInstBarPerc(0);

            std::error_code ec;
            std::filesystem::create_directories("sdmc:/emuiibo/", ec);
            std::filesystem::create_directories(AMIIBO_DIR, ec);

            std::filesystem::remove(AMIIBO_DB_PATH, ec);
            if (!inst::curl::downloadFile(inst::config::amiiboApiUrl, AMIIBO_DB_PATH.c_str(), 0, false)) {
                inst::ui::mainApp->CreateShowDialog("amiibo.db_failed"_lang, "amiibo.db_failed_desc"_lang, {"common.ok"_lang}, true);
                inst::ui::instPage::loadMainMenu();
                return;
            }

            nlohmann::json db;
            try {
                std::ifstream in(AMIIBO_DB_PATH);
                in >> db;
            } catch (std::exception& e) {
                inst::ui::mainApp->CreateShowDialog("amiibo.db_failed"_lang, "amiibo.db_failed_desc"_lang, {"common.ok"_lang}, true);
                inst::ui::instPage::loadMainMenu();
                return;
            }

            if (!db.contains("amiibo") || !db["amiibo"].is_array()) {
                inst::ui::mainApp->CreateShowDialog("amiibo.db_failed"_lang, "amiibo.db_failed_desc"_lang, {"common.ok"_lang}, true);
                inst::ui::instPage::loadMainMenu();
                return;
            }

            std::srand((unsigned)std::time(nullptr));

            const auto& list = db["amiibo"];
            const size_t total = list.size();
            size_t generated = 0;
            size_t backfilled = 0;
            size_t skipped = 0;

            // Throttle progress redraws so we don't render once per amiibo —
            // ~1% increments keep CallForRender cost low across 600+ entries.
            int lastPercent = -1;
            for (size_t i = 0; i < total; i++) {
                const auto& entry = list[i];
                const int percent = (int)((double)(i + 1) / (double)total * 100.0);
                if (percent != lastPercent) {
                    inst::ui::instPage::setInstBarPerc((double)percent);
                    inst::ui::instPage::setInstInfoText(
                        "amiibo.generating"_lang +
                        entry.value("name", std::string("?")) +
                        " (" + std::to_string(i + 1) + "/" + std::to_string(total) + ")");
                    lastPercent = percent;
                }
                switch (generateOne(entry)) {
                    case GenResult::Created:         generated++;  break;
                    case GenResult::ImageBackfilled: backfilled++; break;
                    case GenResult::Skipped:         skipped++;    break;
                }
            }

            inst::ui::instPage::setInstBarPerc(100);

            const std::string doneDesc =
                "amiibo.done_generated"_lang + std::to_string(generated) + "\n" +
                "amiibo.done_backfilled"_lang + std::to_string(backfilled) + "\n" +
                "amiibo.done_skipped"_lang + std::to_string(skipped);
            inst::ui::mainApp->CreateShowDialog("amiibo.done_title"_lang, doneDesc, {"common.ok"_lang}, false);
            inst::ui::instPage::loadMainMenu();
        } catch (std::exception& e) {
            LOG_DEBUG("Amiibo generation failed: %s", e.what());
            inst::ui::mainApp->CreateShowDialog("amiibo.generic_error"_lang, (std::string)e.what(), {"common.ok"_lang}, true);
            inst::ui::instPage::loadMainMenu();
        }
    }
}
