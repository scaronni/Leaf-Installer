#include <filesystem>
#include <switch.h>
#include "ui/MainApplication.hpp"
#include "ui/mainPage.hpp"
#include "util/util.hpp"
#include "util/config.hpp"
#include "util/lang.hpp"
#include "cfwInstall.hpp"
#include "amiiboGen.hpp"
#include "data/buffered_placeholder_writer.hpp"
#include "nx/usbhdd.h"

#define COLOR(hex) pu::ui::Color::FromHex(hex)

namespace inst::ui {
    extern MainApplication *mainApp;
    bool appletFinished = false;
    bool updateFinished = false;
    // Touch tap on the Exit row used to call FadeOut+Close directly from
    // inside Plutonium's Menu::OnInput callback. FadeOut runs an animation
    // loop that calls CallForRender → recursive Application::OnRender →
    // Menu::OnInput → which still sees `item_touched == true` because the
    // outer Menu::OnInput hasn't returned to clear it → fires our callback
    // again → unbounded recursion → stack-overflow crash. The A-button path
    // doesn't trip this because A is consumed in the Layout's onInput before
    // element iteration ever begins. Fix: just flip a flag from the click
    // handler and let mainMenuThread (a render callback that runs at the
    // top of the frame, outside element iteration) do the actual exit.
    bool exitRequested = false;

    void mainMenuThread() {
        bool menuLoaded = mainApp->IsShown();
        if (!appletFinished && appletGetAppletType() == AppletType_LibraryApplet) {
            tin::data::NUM_BUFFER_SEGMENTS = 2;
            if (menuLoaded) {
                inst::ui::appletFinished = true;
                mainApp->CreateShowDialog("main.applet.title"_lang, "main.applet.desc"_lang, {"common.ok"_lang}, true);
            }
        } else if (!appletFinished) {
            inst::ui::appletFinished = true;
            tin::data::NUM_BUFFER_SEGMENTS = 128;
        }
        if (!updateFinished && (!inst::config::autoUpdate || inst::util::getIPAddress() == "1.0.0.127")) updateFinished = true;
        if (!updateFinished && menuLoaded && inst::config::updateInfo.size()) {
            updateFinished = true;
            optionsPage::askToUpdate(inst::config::updateInfo);
        }
        if (exitRequested) {
            exitRequested = false;
            mainApp->FadeOut();
            mainApp->Close();
        }
    }

    MainPage::MainPage() : Layout::Layout() {
        this->SetBackgroundColor(COLOR("#670000FF"));
        this->Add(inst::util::makeBackgroundImage());
        this->topRect = Rectangle::New(0, 0, 1920, 141, COLOR("#170909FF"));
        this->botRect = Rectangle::New(0, 988, 1920, 92, COLOR("#17090980"));
        if (inst::config::noGraphics) {
            this->titleImage = Image::New(-170, 0, inst::util::loadTex("romfs:/images/logo.png"));
            this->titleImage->SetWidth(720);
            this->titleImage->SetHeight(140);
            this->appVersionText = TextBlock::New(550, 74, "v" + inst::config::appVersion);
            this->appVersionText->SetFont(pu::ui::MakeDefaultFontName(33));
        }
        else {
            this->titleImage = Image::New(0, 0, inst::util::loadTex("romfs:/images/logo.png"));
            this->titleImage->SetWidth(720);
            this->titleImage->SetHeight(140);
            this->appVersionText = TextBlock::New(720, 74, "v" + inst::config::appVersion);
            this->appVersionText->SetFont(pu::ui::MakeDefaultFontName(33));
        }
        this->appVersionText->SetColor(COLOR("#FFFFFFFF"));
        this->butText = TextBlock::New(15, 1017, "main.buttons"_lang);
        this->butText->SetFont(pu::ui::MakeDefaultFontName(36));
        this->butText->SetColor(COLOR("#FFFFFFFF"));
        this->optionMenu = pu::ui::elm::Menu::New(0, 142, 1920, COLOR("#67000000"), COLOR("#67000000"), 120, 7);
        this->optionMenu->SetItemsFocusColor(COLOR("#170909C0"));
        this->optionMenu->SetScrollbarColor(COLOR("#170909FF"));
        this->installMenuItem = pu::ui::elm::MenuItem::New("main.menu.sd"_lang);
        this->installMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->installMenuItem->SetIcon(inst::util::loadTex("romfs:/images/icons/micro-sd.png"));
        this->netInstallMenuItem = pu::ui::elm::MenuItem::New("main.menu.net"_lang);
        this->netInstallMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->netInstallMenuItem->SetIcon(inst::util::loadTex("romfs:/images/icons/cloud-download.png"));
        this->usbInstallMenuItem = pu::ui::elm::MenuItem::New("main.menu.usb"_lang);
        this->usbInstallMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->usbInstallMenuItem->SetIcon(inst::util::loadTex("romfs:/images/icons/usb-port.png"));
        this->cfwMenuItem = pu::ui::elm::MenuItem::New("main.menu.cfw"_lang);
        this->cfwMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->cfwMenuItem->SetIcon(inst::util::loadTex("romfs:/images/icons/wrench.png"));
        this->amiiboMenuItem = pu::ui::elm::MenuItem::New("main.menu.amiibo"_lang);
        this->amiiboMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->amiiboMenuItem->SetIcon(inst::util::loadTex("romfs:/images/icons/amiibo.png"));
        this->settingsMenuItem = pu::ui::elm::MenuItem::New("main.menu.set"_lang);
        this->settingsMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->settingsMenuItem->SetIcon(inst::util::loadTex("romfs:/images/icons/settings.png"));
        this->exitMenuItem = pu::ui::elm::MenuItem::New("main.menu.exit"_lang);
        this->exitMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->exitMenuItem->SetIcon(inst::util::loadTex("romfs:/images/icons/exit-run.png"));
        if (std::filesystem::exists(inst::config::appDir + "/leaf_main.png")) this->leafImage = Image::New(615, 285, inst::util::loadTex(inst::config::appDir + "/leaf_main.png"));
        else this->leafImage = Image::New(615, 285, inst::util::loadTex("romfs:/images/leaf_main.png"));
        this->leafImage->SetWidth(1296);
        this->leafImage->SetHeight(732);
        this->Add(this->topRect);
        this->Add(this->botRect);
        this->Add(this->titleImage);
        this->Add(this->appVersionText);
        inst::util::addStorageInfoBlocks(this, 1900, 25);
        this->Add(this->butText);
        this->optionMenu->AddItem(this->installMenuItem);
        this->optionMenu->AddItem(this->netInstallMenuItem);
        this->optionMenu->AddItem(this->usbInstallMenuItem);
        this->optionMenu->AddItem(this->cfwMenuItem);
        this->optionMenu->AddItem(this->amiiboMenuItem);
        this->optionMenu->AddItem(this->settingsMenuItem);
        this->optionMenu->AddItem(this->exitMenuItem);
        // Plutonium's Menu eats touch events internally and fires per-item
        // callbacks bound to TouchPseudoKey — it does NOT propagate that
        // pseudo-key up to the Layout's onInput. The `Up & TouchPseudoKey`
        // check in onInput is therefore dead code for touch, and tapping an
        // item only moves the highlight. Register a TouchPseudoKey callback
        // on each item so taps actually activate.
        this->installMenuItem->AddOnKey([this]() { this->installMenuItem_Click(); }, pu::ui::TouchPseudoKey);
        this->netInstallMenuItem->AddOnKey([this]() { this->netInstallMenuItem_Click(); }, pu::ui::TouchPseudoKey);
        this->usbInstallMenuItem->AddOnKey([this]() { this->usbInstallMenuItem_Click(); }, pu::ui::TouchPseudoKey);
        this->cfwMenuItem->AddOnKey([this]() { this->cfwMenuItem_Click(); }, pu::ui::TouchPseudoKey);
        this->amiiboMenuItem->AddOnKey([this]() { this->amiiboMenuItem_Click(); }, pu::ui::TouchPseudoKey);
        this->settingsMenuItem->AddOnKey([this]() { this->settingsMenuItem_Click(); }, pu::ui::TouchPseudoKey);
        this->exitMenuItem->AddOnKey([this]() { this->exitMenuItem_Click(); }, pu::ui::TouchPseudoKey);
        this->Add(this->optionMenu);
        this->Add(this->leafImage);
        this->leafImage->SetVisible(!inst::config::noGraphics);
        this->AddRenderCallback(mainMenuThread);
    }

    void MainPage::installMenuItem_Click() {
        const char* startPath = "sdmc:/";
        if (nx::hdd::count() && nx::hdd::rootPath()) startPath = nx::hdd::rootPath();
        mainApp->sdinstPage->drawMenuItems(true, startPath);
        mainApp->sdinstPage->menu->SetSelectedIndex(0);
        mainApp->LoadLayout(mainApp->sdinstPage);
    }

    void MainPage::netInstallMenuItem_Click() {
        if (inst::util::getIPAddress() == "1.0.0.127") {
            inst::ui::mainApp->CreateShowDialog("main.net.title"_lang, "main.net.desc"_lang, {"common.ok"_lang}, true);
            return;
        }
        mainApp->netinstPage->startNetwork();
    }

    void MainPage::usbInstallMenuItem_Click() {
        if (inst::util::usbIsConnected()) mainApp->usbinstPage->startUsb();
        else mainApp->CreateShowDialog("main.usb.error.title"_lang, "main.usb.error.desc"_lang, {"common.ok"_lang}, false);
    }

    void MainPage::cfwMenuItem_Click() {
        cfw::startCfwInstall();
    }

    void MainPage::amiiboMenuItem_Click() {
        amiibo::generateAmiiboData();
    }

    void MainPage::exitMenuItem_Click() {
        // Direct FadeOut+Close from a callback that fires inside Plutonium's
        // element-iteration loop (i.e. the touch path) recurses unboundedly
        // — see the note next to `exitRequested` at the top of this file.
        // The A-button path works fine without deferring because A is
        // consumed in Layout::onInput before element iteration runs, but we
        // route both through the same flag for consistency.
        exitRequested = true;
    }

    void MainPage::settingsMenuItem_Click() {
        mainApp->LoadLayout(mainApp->optionspage);
    }

    void MainPage::onInput(u64 Down, u64 Up, u64 Held, pu::ui::TouchPoint Pos) {
        if (((Down & HidNpadButton_Plus) || (Down & HidNpadButton_Minus) || (Down & HidNpadButton_B)) && mainApp->IsShown()) {
            exitRequested = true;
        }
        if ((Down & HidNpadButton_A) || (Up & pu::ui::TouchPseudoKey)) {
            switch (this->optionMenu->GetSelectedIndex()) {
                case 0:
                    this->installMenuItem_Click();
                    break;
                case 1:
                    this->netInstallMenuItem_Click();
                    break;
                case 2:
                    MainPage::usbInstallMenuItem_Click();
                    break;
                case 3:
                    MainPage::cfwMenuItem_Click();
                    break;
                case 4:
                    MainPage::amiiboMenuItem_Click();
                    break;
                case 5:
                    MainPage::settingsMenuItem_Click();
                    break;
                case 6:
                    MainPage::exitMenuItem_Click();
                    break;
                default:
                    break;
            }
        }
    }
}
