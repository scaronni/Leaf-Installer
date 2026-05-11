#include <thread>
#include "switch.h"
#include "util/error.hpp"
#include "ui/MainApplication.hpp"
#include "util/util.hpp"
#include "util/config.hpp"

using namespace pu::ui::render;
int main(int argc, char* argv[])
{
    inst::util::initApp();
    try {
        RendererInitOptions init_opts(
            SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC | SDL_INIT_GAMECONTROLLER,
            RendererHardwareFlags);
        init_opts.UseImage(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP);
        init_opts.UseRomfs();
        init_opts.SetPlServiceType(PlServiceType_User);
        init_opts.AddDefaultAllSharedFonts();
        init_opts.AddExtraDefaultFontSize(33);
        init_opts.AddExtraDefaultFontSize(36);
        init_opts.AddInputNpadStyleTag(HidNpadStyleSet_NpadStandard);
        init_opts.AddInputNpadIdType(HidNpadIdType_Handheld);
        init_opts.AddInputNpadIdType(HidNpadIdType_No1);
        auto renderer = Renderer::New(init_opts);
        auto main = inst::ui::MainApplication::New(renderer);
        std::thread updateThread;
        if (inst::config::autoUpdate && inst::util::getIPAddress() != "1.0.0.127") updateThread = std::thread(inst::util::checkForAppUpdate);
        main->Load();
        main->ShowWithFadeIn();
        updateThread.join();
    } catch (std::exception& e) {
        LOG_DEBUG("An error occurred:\n%s", e.what());
    }
    inst::util::deinitApp();
    return 0;
}
