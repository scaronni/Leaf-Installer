#include <fstream>
#include <iomanip>
#include "util/config.hpp"
#include "util/json.hpp"

namespace inst::config {
    std::string gAuthKey;
    std::string leafUrl;
    std::string ultrahandUrl;
    std::string sysPatchUrl;
    std::string emuiiboUrl;
    std::string atmosphereUrl;
    std::string hekateUrl;
    std::string amiiboApiUrl;
    std::string lastNetUrl;
    std::vector<std::string> updateInfo;
    std::map<std::string, std::string> cfwInstalled;
    int languageSetting;
    bool autoUpdate;
    bool deletePrompt;
    bool noGraphics;
    bool ignoreReqVers;
    bool overClock;
    bool validateNCAs;

    void setConfig() {
        nlohmann::json j = {
            {"autoUpdate", autoUpdate},
            {"deletePrompt", deletePrompt},
            {"gAuthKey", gAuthKey},
            {"noGraphics", noGraphics},
            {"ignoreReqVers", ignoreReqVers},
            {"languageSetting", languageSetting},
            {"overClock", overClock},
            {"leafUrl", leafUrl},
            {"ultrahandUrl", ultrahandUrl},
            {"sysPatchUrl", sysPatchUrl},
            {"emuiiboUrl", emuiiboUrl},
            {"atmosphereUrl", atmosphereUrl},
            {"hekateUrl", hekateUrl},
            {"amiiboApiUrl", amiiboApiUrl},
            {"validateNCAs", validateNCAs},
            {"lastNetUrl", lastNetUrl},
            {"cfwInstalled", cfwInstalled}
        };
        std::ofstream file(inst::config::configPath);
        file << std::setw(4) << j << std::endl;
    }

    void parseConfig() {
        try {
            std::ifstream file(inst::config::configPath);
            nlohmann::json j;
            file >> j;
            autoUpdate = j["autoUpdate"].get<bool>();
            deletePrompt = j["deletePrompt"].get<bool>();
            gAuthKey = j["gAuthKey"].get<std::string>();
            noGraphics = j["noGraphics"].get<bool>();
            ignoreReqVers = j["ignoreReqVers"].get<bool>();
            languageSetting = j["languageSetting"].get<int>();
            overClock = j["overClock"].get<bool>();
            leafUrl = j["leafUrl"].get<std::string>();
            ultrahandUrl = j["ultrahandUrl"].get<std::string>();
            sysPatchUrl = j["sysPatchUrl"].get<std::string>();
            emuiiboUrl = j["emuiiboUrl"].get<std::string>();
            atmosphereUrl = j["atmosphereUrl"].get<std::string>();
            hekateUrl = j["hekateUrl"].get<std::string>();
            amiiboApiUrl = j["amiiboApiUrl"].get<std::string>();
            validateNCAs = j["validateNCAs"].get<bool>();
            lastNetUrl = j["lastNetUrl"].get<std::string>();
            cfwInstalled = j["cfwInstalled"].get<std::map<std::string, std::string>>();
        }
        catch (...) {
            // If loading values from the config fails, we just load the defaults and overwrite the old config
            gAuthKey = {0x41,0x49,0x7a,0x61,0x53,0x79,0x42,0x4d,0x71,0x76,0x34,0x64,0x58,0x6e,0x54,0x4a,0x4f,0x47,0x51,0x74,0x5a,0x5a,0x53,0x33,0x43,0x42,0x6a,0x76,0x66,0x37,0x34,0x38,0x51,0x76,0x78,0x53,0x7a,0x46,0x30};
            leafUrl = "https://github.com/scaronni/Leaf-Installer/releases";
            ultrahandUrl = "https://github.com/ppkantorski/Ultrahand-Overlay/releases";
            sysPatchUrl = "https://github.com/impeeza/sys-patch/releases";
            emuiiboUrl = "https://github.com/XorTroll/emuiibo/releases";
            atmosphereUrl = "https://github.com/Atmosphere-NX/Atmosphere/releases";
            hekateUrl = "https://github.com/CTCaer/hekate/releases";
            amiiboApiUrl = "https://amiiboapi.org/api/amiibo/";
            languageSetting = 99;
            autoUpdate = true;
            deletePrompt = true;
            noGraphics = false;
            ignoreReqVers = true;
            overClock = false;
            validateNCAs = true;
            lastNetUrl = "https://";
            cfwInstalled = {};
            setConfig();
        }
    }
}