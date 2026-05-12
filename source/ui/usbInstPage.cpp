#include "ui/usbInstPage.hpp"
#include "ui/MainApplication.hpp"
#include "util/util.hpp"
#include "util/config.hpp"
#include "util/lang.hpp"
#include "util/usb_util.hpp"
#include "usbInstall.hpp"


#define COLOR(hex) pu::ui::Color::FromHex(hex)

namespace inst::ui {
    extern MainApplication *mainApp;

    usbInstPage::usbInstPage() : Layout::Layout() {
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
        this->pageInfoText = TextBlock::New(15, 164, "");
        this->pageInfoText->SetFont(pu::ui::MakeDefaultFontName(45));
        this->pageInfoText->SetColor(COLOR("#FFFFFFFF"));
        this->butText = TextBlock::New(15, 1017, "");
        this->butText->SetFont(pu::ui::MakeDefaultFontName(36));
        this->butText->SetColor(COLOR("#FFFFFFFF"));
        this->menu = pu::ui::elm::Menu::New(0, 234, 1920, COLOR("#FFFFFF00"), COLOR("#FFFFFF00"), 108, 7);
        this->menu->SetItemsFocusColor(COLOR("#170909C0"));
        this->menu->SetScrollbarColor(COLOR("#17090980"));
        this->infoImage = Image::New(690, 498, inst::util::loadTex("romfs:/images/icons/usb-connection-waiting.png"));
        this->infoImage->SetWidth(540);
        this->infoImage->SetHeight(129);
        this->Add(this->topRect);
        this->Add(this->infoRect);
        this->Add(this->botRect);
        this->Add(this->titleImage);
        this->Add(this->appVersionText);
        inst::util::addStorageInfoBlocks(this, 1900, 25);
        this->Add(this->butText);
        this->Add(this->pageInfoText);
        this->Add(this->menu);
        this->Add(this->infoImage);
    }

    void usbInstPage::drawMenuItems(bool clearItems) {
        if (clearItems) this->selectedTitles = {};
        this->menu->ClearItems();
        for (auto& url: this->ourTitles) {
            std::string itm = inst::util::shortenString(inst::util::formatUrlString(url), 56, true);
            auto ourEntry = pu::ui::elm::MenuItem::New(itm);
            ourEntry->SetColor(COLOR("#FFFFFFFF"));
            ourEntry->SetIcon(inst::util::loadTex("romfs:/images/icons/checkbox-blank-outline.png"));
            for (long unsigned int i = 0; i < this->selectedTitles.size(); i++) {
                if (this->selectedTitles[i] == url) {
                    ourEntry->SetIcon(inst::util::loadTex("romfs:/images/icons/check-box-outline.png"));
                }
            }
            this->menu->AddItem(ourEntry);
        }
    }

    void usbInstPage::selectTitle(int selectedIndex) {
        if (this->menu->GetItems()[selectedIndex]->GetIconTexture() == inst::util::loadTex("romfs:/images/icons/check-box-outline.png")) {
            for (long unsigned int i = 0; i < this->selectedTitles.size(); i++) {
                if (this->selectedTitles[i] == this->ourTitles[selectedIndex]) this->selectedTitles.erase(this->selectedTitles.begin() + i);
            }
        } else this->selectedTitles.push_back(this->ourTitles[selectedIndex]);
        this->drawMenuItems(false);
    }

    void usbInstPage::startUsb() {
        this->pageInfoText->SetText("inst.usb.top_info"_lang);
        this->butText->SetText("inst.usb.buttons"_lang);
        this->menu->SetVisible(false);
        this->menu->ClearItems();
        this->infoImage->SetVisible(true);
        mainApp->LoadLayout(mainApp->usbinstPage);
        mainApp->CallForRender();
        this->ourTitles = usbInstStuff::OnSelected();
        if (!this->ourTitles.size()) {
            mainApp->LoadLayout(mainApp->mainPage);
            return;
        } else {
            mainApp->CallForRender(); // If we re-render a few times during this process the main screen won't flicker
            this->pageInfoText->SetText("inst.usb.top_info2"_lang);
            this->butText->SetText("inst.usb.buttons2"_lang);
            this->drawMenuItems(true);
            this->menu->SetSelectedIndex(0);
            mainApp->CallForRender();
            this->infoImage->SetVisible(false);
            this->menu->SetVisible(true);
        }
        return;
    }

    void usbInstPage::startInstall() {
        int dialogResult = -1;
        if (this->selectedTitles.size() == 1) dialogResult = mainApp->CreateShowDialog("inst.target.desc0"_lang + "inst.target.desc1"_lang, "common.cancel_desc"_lang, {"inst.target.opt0"_lang, "inst.target.opt1"_lang}, false);
        else dialogResult = mainApp->CreateShowDialog("inst.target.desc00"_lang + std::to_string(this->selectedTitles.size()) + "inst.target.desc01"_lang, "common.cancel_desc"_lang, {"inst.target.opt0"_lang, "inst.target.opt1"_lang}, false);
        if (dialogResult == -1) return;
        usbInstStuff::installTitleUsb(this->selectedTitles, dialogResult);
        return;
    }

    void usbInstPage::onInput(u64 Down, u64 Up, u64 Held, pu::ui::TouchPoint Pos) {
        if (Down & HidNpadButton_B) {
            tin::util::USBCmdManager::SendExitCmd();
            mainApp->LoadLayout(mainApp->mainPage);
        }
        if ((Down & HidNpadButton_A) || (Up & pu::ui::TouchPseudoKey)) {
            this->selectTitle(this->menu->GetSelectedIndex());
            if (this->menu->GetItems().size() == 1 && this->selectedTitles.size() == 1) {
                this->startInstall();
            }
        }
        if ((Down & HidNpadButton_Y)) {
            if (this->selectedTitles.size() == this->menu->GetItems().size()) this->drawMenuItems(true);
            else {
                for (long unsigned int i = 0; i < this->menu->GetItems().size(); i++) {
                    if (this->menu->GetItems()[i]->GetIconTexture() == inst::util::loadTex("romfs:/images/icons/check-box-outline.png")) continue;
                    else this->selectTitle(i);
                }
                this->drawMenuItems(false);
            }
        }
        if (Down & HidNpadButton_Plus) {
            if (this->selectedTitles.size() == 0) {
                this->selectTitle(this->menu->GetSelectedIndex());
                this->startInstall();
                return;
            }
            this->startInstall();
        }
    }
}