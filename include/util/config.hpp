#pragma once

#include <map>
#include <string>
#include <vector>

namespace inst::config {
    static const std::string appDir = "sdmc:/switch/Leaf-Installer";
    static const std::string configPath = appDir + "/config.json";
    static const std::string appVersion = std::string(APP_VERSION);

    extern std::string gAuthKey;
    extern std::string leafUrl;
    extern std::string ultrahandUrl;
    extern std::string sysPatchUrl;
    extern std::string emuiiboUrl;
    extern std::string atmosphereUrl;
    extern std::string hekateUrl;
    extern std::string amiiboApiUrl;
    extern std::string lastNetUrl;
    extern std::vector<std::string> updateInfo;
    extern std::map<std::string, std::string> cfwInstalled;
    extern int languageSetting;
    extern bool ignoreReqVers;
    extern bool validateNCAs;
    extern bool overClock;
    extern bool deletePrompt;
    extern bool autoUpdate;
    extern bool noGraphics;
    extern bool usbAck;

    void setConfig();
    void parseConfig();
}