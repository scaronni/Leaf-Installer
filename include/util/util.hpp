#pragma once
#include <filesystem>
#include <pu/Plutonium>

namespace inst::util {
    void initApp ();
    void deinitApp ();
    pu::sdl2::TextureHandle::Ref loadTex(const std::string& path);
    pu::ui::elm::Image::Ref makeBackgroundImage();
    void initInstallServices();
    void deinitInstallServices();
    bool ignoreCaseCompare(const std::string &a, const std::string &b);
    std::vector<std::filesystem::path> getDirectoryFiles(const std::string & dir, const std::vector<std::string> & extensions);
    std::vector<std::filesystem::path> getDirsAtPath(const std::string & dir);
    bool removeDirectory(std::string dir);
    bool copyFile(std::string inFile, std::string outFile);
    std::string formatUrlString(std::string ourString);
    std::string formatUrlLink(std::string ourString);
    std::string shortenString(std::string ourString, int ourLength, bool isFile);
    std::string readTextFromFile(std::string ourFile);
    std::string softwareKeyboard(std::string guideText, std::string initialText, int LenMax);
    std::string getDriveFileName(std::string fileId);
    std::vector<uint32_t> setClockSpeed(int deviceToClock, uint32_t clockSpeed);
    std::string getIPAddress();
    bool usbIsConnected();
    void playAudio(std::string audioPath);
    std::vector<std::string> checkForAppUpdate();
    std::vector<std::string> fetchLatestRelease(const std::string& releasesPageUrl);
    // Returns a "<free> / <total>" string (e.g. "32.5 GB / 64.0 GB") for the
    // given NcmStorageId — used to surface available space on each page's top bar.
    std::string getStorageInfoText(NcmStorageId storageId);
    // Builds two right-aligned TextBlocks ("System memory: …" and "microSD card: …")
    // and adds them to the given layout. `right_x` is the right edge to align
    // against (e.g. 1900 leaves ~20 px margin from the 1920-wide screen). The
    // blocks are tracked in a private registry so refreshAllStorageDisplays()
    // can update them after each install without per-page member plumbing.
    void addStorageInfoBlocks(pu::ui::Layout* layout, s32 right_x, s32 y);
    // Re-queries storage sizes and updates every tracked storage display.
    void refreshAllStorageDisplays();
}