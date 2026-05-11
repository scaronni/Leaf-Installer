#include <filesystem>
#include <switch.h>
#include "ui/MainApplication.hpp"
#include "ui/mainPage.hpp"
#include "ui/instPage.hpp"
#include "ui/optionsPage.hpp"
#include "util/util.hpp"
#include "util/config.hpp"
#include "util/curl.hpp"
#include "util/unzip.hpp"
#include "util/lang.hpp"
#include "ui/instPage.hpp"

#define COLOR(hex) pu::ui::Color::FromHex(hex)

namespace inst::ui {
    extern MainApplication *mainApp;

    std::vector<std::string> languageStrings = {"English", "日本語", "Français", "Deutsch", "Italiano", "Español", "Português", "한국어", "Русский", "簡体中文","繁體中文"};

    optionsPage::optionsPage() : Layout::Layout() {
        this->SetBackgroundColor(COLOR("#670000FF"));
        this->Add(inst::util::makeBackgroundImage());
        this->topRect = Rectangle::New(0, 0, 1920, 141, COLOR("#170909FF"));
        this->infoRect = Rectangle::New(0, 142, 1920, 90, COLOR("#17090980"));
        this->botRect = Rectangle::New(0, 990, 1920, 90, COLOR("#17090980"));
        if (inst::config::gayMode) {
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
        this->pageInfoText = TextBlock::New(15, 164, "options.title"_lang);
        this->pageInfoText->SetFont(pu::ui::MakeDefaultFontName(45));
        this->pageInfoText->SetColor(COLOR("#FFFFFFFF"));
        this->butText = TextBlock::New(15, 1017, "options.buttons"_lang);
        this->butText->SetFont(pu::ui::MakeDefaultFontName(36));
        this->butText->SetColor(COLOR("#FFFFFFFF"));
        this->menu = pu::ui::elm::Menu::New(0, 234, 1920, COLOR("#FFFFFF00"), COLOR("#FFFFFF00"), 126, (506 / 84));
        this->menu->SetItemsFocusColor(COLOR("#00000033"));
        this->menu->SetScrollbarColor(COLOR("#17090980"));
        this->Add(this->topRect);
        this->Add(this->infoRect);
        this->Add(this->botRect);
        this->Add(this->titleImage);
        this->Add(this->appVersionText);
        this->Add(this->butText);
        this->Add(this->pageInfoText);
        this->setMenuText();
        this->Add(this->menu);
    }

    void optionsPage::askToUpdate(std::vector<std::string> updateInfo) {
            if (!mainApp->CreateShowDialog("options.update.title"_lang, "options.update.desc0"_lang + updateInfo[0] + "options.update.desc1"_lang, {"options.update.opt0"_lang, "common.cancel"_lang}, false)) {
                inst::ui::instPage::loadInstallScreen();
                inst::ui::instPage::setTopInstInfoText("options.update.top_info"_lang + updateInfo[0]);
                inst::ui::instPage::setInstBarPerc(0);
                inst::ui::instPage::setInstInfoText("options.update.bot_info"_lang + updateInfo[0]);
                try {
                    std::string downloadName = inst::config::appDir + "/temp_download.zip";
                    inst::curl::downloadFile(updateInfo[1], downloadName.c_str(), 0, true);
                    romfsExit();
                    inst::ui::instPage::setInstInfoText("options.update.bot_info2"_lang + updateInfo[0]);
                    inst::zip::extractFile(downloadName, "sdmc:/");
                    std::filesystem::remove(downloadName);
                    mainApp->CreateShowDialog("options.update.complete"_lang, "options.update.end_desc"_lang, {"common.ok"_lang}, false);
                } catch (...) {
                    mainApp->CreateShowDialog("options.update.failed"_lang, "options.update.end_desc"_lang, {"common.ok"_lang}, false);
                }
                mainApp->FadeOut();
                mainApp->Close();
            }
        return;
    }

    std::string optionsPage::getMenuOptionIcon(bool ourBool) {
        if(ourBool) return "romfs:/images/icons/check-box-outline.png";
        else return "romfs:/images/icons/checkbox-blank-outline.png";
    }

    std::string optionsPage::getMenuLanguage(int ourLangCode) {
        switch (ourLangCode) {
            case 1:
            case 12:
                return languageStrings[0];
            case 0:
                return languageStrings[1];
            case 2:
            case 13:
                return languageStrings[2];
            case 3:
                return languageStrings[3];
            case 4:
                return languageStrings[4];
            case 5:
            case 14:
                return languageStrings[5];
            case 9:
                return languageStrings[6];
            case 7:
                return languageStrings[7];
            case 10:
                return languageStrings[8];
            case 6:
                return languageStrings[9];
            case 11:
                return languageStrings[10];
            default:
                return "options.language.system_language"_lang;
        }
    }

    void optionsPage::setMenuText() {
        this->menu->ClearItems();
        auto ignoreFirmOption = pu::ui::elm::MenuItem::New("options.menu_items.ignore_firm"_lang);
        ignoreFirmOption->SetColor(COLOR("#FFFFFFFF"));
        ignoreFirmOption->SetIcon(inst::util::loadTex(this->getMenuOptionIcon(inst::config::ignoreReqVers)));
        this->menu->AddItem(ignoreFirmOption);
        auto validateOption = pu::ui::elm::MenuItem::New("options.menu_items.nca_verify"_lang);
        validateOption->SetColor(COLOR("#FFFFFFFF"));
        validateOption->SetIcon(inst::util::loadTex(this->getMenuOptionIcon(inst::config::validateNCAs)));
        this->menu->AddItem(validateOption);
        auto overclockOption = pu::ui::elm::MenuItem::New("options.menu_items.boost_mode"_lang);
        overclockOption->SetColor(COLOR("#FFFFFFFF"));
        overclockOption->SetIcon(inst::util::loadTex(this->getMenuOptionIcon(inst::config::overClock)));
        this->menu->AddItem(overclockOption);
        auto deletePromptOption = pu::ui::elm::MenuItem::New("options.menu_items.ask_delete"_lang);
        deletePromptOption->SetColor(COLOR("#FFFFFFFF"));
        deletePromptOption->SetIcon(inst::util::loadTex(this->getMenuOptionIcon(inst::config::deletePrompt)));
        this->menu->AddItem(deletePromptOption);
        auto autoUpdateOption = pu::ui::elm::MenuItem::New("options.menu_items.auto_update"_lang);
        autoUpdateOption->SetColor(COLOR("#FFFFFFFF"));
        autoUpdateOption->SetIcon(inst::util::loadTex(this->getMenuOptionIcon(inst::config::autoUpdate)));
        this->menu->AddItem(autoUpdateOption);
        auto gayModeOption = pu::ui::elm::MenuItem::New("options.menu_items.gay_option"_lang);
        gayModeOption->SetColor(COLOR("#FFFFFFFF"));
        gayModeOption->SetIcon(inst::util::loadTex(this->getMenuOptionIcon(inst::config::gayMode)));
        this->menu->AddItem(gayModeOption);
        auto awooUrlOption = pu::ui::elm::MenuItem::New("options.menu_items.awoo_url"_lang + inst::util::shortenString(inst::config::awooUrl, 80, false));
        awooUrlOption->SetColor(COLOR("#FFFFFFFF"));
        this->menu->AddItem(awooUrlOption);
        auto ultrahandUrlOption = pu::ui::elm::MenuItem::New("options.menu_items.ultrahand_url"_lang + inst::util::shortenString(inst::config::ultrahandUrl, 80, false));
        ultrahandUrlOption->SetColor(COLOR("#FFFFFFFF"));
        this->menu->AddItem(ultrahandUrlOption);
        auto sysPatchUrlOption = pu::ui::elm::MenuItem::New("options.menu_items.sys_patch_url"_lang + inst::util::shortenString(inst::config::sysPatchUrl, 80, false));
        sysPatchUrlOption->SetColor(COLOR("#FFFFFFFF"));
        this->menu->AddItem(sysPatchUrlOption);
        auto languageOption = pu::ui::elm::MenuItem::New("options.menu_items.language"_lang + this->getMenuLanguage(inst::config::languageSetting));
        languageOption->SetColor(COLOR("#FFFFFFFF"));
        this->menu->AddItem(languageOption);
        auto updateOption = pu::ui::elm::MenuItem::New("options.menu_items.check_update"_lang);
        updateOption->SetColor(COLOR("#FFFFFFFF"));
        this->menu->AddItem(updateOption);
        auto creditsOption = pu::ui::elm::MenuItem::New("options.menu_items.credits"_lang);
        creditsOption->SetColor(COLOR("#FFFFFFFF"));
        this->menu->AddItem(creditsOption);
    }

    void optionsPage::onInput(u64 Down, u64 Up, u64 Held, pu::ui::TouchPoint Pos) {
        if (Down & HidNpadButton_B) {
            mainApp->LoadLayout(mainApp->mainPage);
        }
        if ((Down & HidNpadButton_A) || (Up & pu::ui::TouchPseudoKey)) {
            std::string keyboardResult;
            int rc;
            std::vector<std::string> downloadUrl;
            std::vector<std::string> languageList;
            switch (this->menu->GetSelectedIndex()) {
                case 0:
                    inst::config::ignoreReqVers = !inst::config::ignoreReqVers;
                    inst::config::setConfig();
                    this->setMenuText();
                    break;
                case 1:
                    if (inst::config::validateNCAs) {
                        if (inst::ui::mainApp->CreateShowDialog("options.nca_warn.title"_lang, "options.nca_warn.desc"_lang, {"common.cancel"_lang, "options.nca_warn.opt1"_lang}, false) == 1) inst::config::validateNCAs = false;
                    } else inst::config::validateNCAs = true;
                    inst::config::setConfig();
                    this->setMenuText();
                    break;
                case 2:
                    inst::config::overClock = !inst::config::overClock;
                    inst::config::setConfig();
                    this->setMenuText();
                    break;
                case 3:
                    inst::config::deletePrompt = !inst::config::deletePrompt;
                    inst::config::setConfig();
                    this->setMenuText();
                    break;
                case 4:
                    inst::config::autoUpdate = !inst::config::autoUpdate;
                    inst::config::setConfig();
                    this->setMenuText();
                    break;
                case 5:
                    if (inst::config::gayMode) {
                        inst::config::gayMode = false;
                        mainApp->mainPage->awooImage->SetVisible(true);
                        mainApp->instpage->awooImage->SetVisible(true);
                        mainApp->instpage->titleImage->SetX(0);
                        mainApp->instpage->appVersionText->SetX(720);
                        mainApp->mainPage->titleImage->SetX(0);
                        mainApp->mainPage->appVersionText->SetX(720);
                        mainApp->netinstPage->titleImage->SetX(0);
                        mainApp->netinstPage->appVersionText->SetX(720);
                        mainApp->optionspage->titleImage->SetX(0);
                        mainApp->optionspage->appVersionText->SetX(720);
                        mainApp->sdinstPage->titleImage->SetX(0);
                        mainApp->sdinstPage->appVersionText->SetX(720);
                        mainApp->usbinstPage->titleImage->SetX(0);
                        mainApp->usbinstPage->appVersionText->SetX(720);
                    }
                    else {
                        inst::config::gayMode = true;
                        mainApp->mainPage->awooImage->SetVisible(false);
                        mainApp->instpage->awooImage->SetVisible(false);
                        mainApp->instpage->titleImage->SetX(-170);
                        mainApp->instpage->appVersionText->SetX(550);
                        mainApp->mainPage->titleImage->SetX(-170);
                        mainApp->mainPage->appVersionText->SetX(550);
                        mainApp->netinstPage->titleImage->SetX(-170);
                        mainApp->netinstPage->appVersionText->SetX(550);
                        mainApp->optionspage->titleImage->SetX(-170);
                        mainApp->optionspage->appVersionText->SetX(550);
                        mainApp->sdinstPage->titleImage->SetX(-170);
                        mainApp->sdinstPage->appVersionText->SetX(550);
                        mainApp->usbinstPage->titleImage->SetX(-170);
                        mainApp->usbinstPage->appVersionText->SetX(550);
                    }
                    inst::config::setConfig();
                    this->setMenuText();
                    break;
                case 6:
                    keyboardResult = inst::util::softwareKeyboard("options.awoo_hint"_lang, inst::config::awooUrl.c_str(), 500);
                    if (keyboardResult.size() > 0) {
                        inst::config::awooUrl = keyboardResult;
                        inst::config::setConfig();
                        this->setMenuText();
                    }
                    break;
                case 7:
                    keyboardResult = inst::util::softwareKeyboard("options.ultrahand_hint"_lang, inst::config::ultrahandUrl.c_str(), 500);
                    if (keyboardResult.size() > 0) {
                        inst::config::ultrahandUrl = keyboardResult;
                        inst::config::setConfig();
                        this->setMenuText();
                    }
                    break;
                case 8:
                    keyboardResult = inst::util::softwareKeyboard("options.sys_patch_hint"_lang, inst::config::sysPatchUrl.c_str(), 500);
                    if (keyboardResult.size() > 0) {
                        inst::config::sysPatchUrl = keyboardResult;
                        inst::config::setConfig();
                        this->setMenuText();
                    }
                    break;
                case 9:
                    languageList = languageStrings;
                    languageList.push_back("options.language.system_language"_lang);
                    rc = inst::ui::mainApp->CreateShowDialog("options.language.title"_lang, "options.language.desc"_lang, languageList, false);
                    if (rc == -1) break;
                    switch(rc) {
                        case 0:
                            inst::config::languageSetting = 1;
                            break;
                        case 1:
                            inst::config::languageSetting = 0;
                            break;
                        case 2:
                            inst::config::languageSetting = 2;
                            break;
                        case 3:
                            inst::config::languageSetting = 3;
                            break;
                        case 4:
                            inst::config::languageSetting = 4;
                            break;
                        case 5:
                            inst::config::languageSetting = 14;
                            break;
                        case 6:
                            inst::config::languageSetting = 9;
                            break;
                        case 7:
                            inst::config::languageSetting = 7;
                            break;
                        case 8:
                            inst::config::languageSetting = 10;
                            break;
                        case 9:
                            inst::config::languageSetting = 6;
                            break;
                        case 10:
                            inst::config::languageSetting = 11;
                            break;
                        default:
                            inst::config::languageSetting = 99;
                    }
                    inst::config::setConfig();
                    mainApp->FadeOut();
                    mainApp->Close();
                    break;
                case 10:
                    if (inst::util::getIPAddress() == "1.0.0.127") {
                        inst::ui::mainApp->CreateShowDialog("main.net.title"_lang, "main.net.desc"_lang, {"common.ok"_lang}, true);
                        break;
                    }
                    downloadUrl = inst::util::checkForAppUpdate();
                    if (!downloadUrl.size()) {
                        mainApp->CreateShowDialog("options.update.title_check_fail"_lang, "options.update.desc_check_fail"_lang, {"common.ok"_lang}, false);
                        break;
                    }
                    this->askToUpdate(downloadUrl);
                    break;
                case 11:
                    inst::ui::mainApp->CreateShowDialog("options.credits.title"_lang, "options.credits.desc"_lang, {"common.close"_lang}, true);
                    break;
                default:
                    break;
            }
        }
    }
}
