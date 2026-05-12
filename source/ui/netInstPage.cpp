#include <filesystem>
#include <switch.h>
#include "ui/MainApplication.hpp"
#include "ui/mainPage.hpp"
#include "ui/netInstPage.hpp"
#include "util/util.hpp"
#include "util/config.hpp"
#include "util/curl.hpp"
#include "util/lang.hpp"
#include "netInstall.hpp"

#define COLOR(hex) pu::ui::Color::FromHex(hex)

namespace inst::ui {
    extern MainApplication* mainApp;

    std::string sourceString = "";

    netInstPage::netInstPage() : Layout::Layout() {
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
        } else {
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
        this->infoImage = Image::New(680, 438, inst::util::loadTex("romfs:/images/icons/lan-connection-waiting.png"));
        this->infoImage->SetWidth(540);
        this->infoImage->SetHeight(209);
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

    void netInstPage::drawMenuItems(bool clearItems) {
        if (clearItems) {
            this->selectedUrls.clear();
            this->alternativeNames.clear();
        }
        this->menu->ClearItems();
        if (this->lastFileId.size() > 1) {
            std::string itm = "..";
            auto ourEntry = pu::ui::elm::MenuItem::New(itm);
            ourEntry->SetColor(COLOR("#FFFFFFFF"));
            ourEntry->SetIcon(inst::util::loadTex("romfs:/images/icons/folder-upload.png"));
            this->menu->AddItem(ourEntry);
        }
        for (auto& url : this->ourUrls) {
            std::string itm = inst::util::shortenString(url.name, 56, true);
            auto ourEntry = pu::ui::elm::MenuItem::New(itm);
            ourEntry->SetColor(COLOR("#FFFFFFFF"));
            if (url.folder) {
                ourEntry->SetIcon(inst::util::loadTex("romfs:/images/icons/folder.png"));
            } else {
                ourEntry->SetIcon(inst::util::loadTex("romfs:/images/icons/checkbox-blank-outline.png"));
                for (long unsigned int i = 0; i < this->selectedUrls.size(); i++) {
                    if (this->selectedUrls[i] == url.id) {
                        ourEntry->SetIcon(inst::util::loadTex("romfs:/images/icons/check-box-outline.png"));
                    }
                }
            }
            this->menu->AddItem(ourEntry);
        }
    }

    void netInstPage::selectTitle(int selectedIndex) {
        std::string fileId;
        int dirListSize = this->lastFileId.size() > 1 ? 1 : 0;
        if (selectedIndex < dirListSize) {
            this->lastFileId.pop_back();
            fileId = this->lastFileId.back();
        } else {
            fileId = this->ourUrls[selectedIndex - dirListSize].id;
        }

        if (this->menu->GetItems()[selectedIndex]->GetIconTexture() == inst::util::loadTex("romfs:/images/icons/check-box-outline.png")) {
            for (long unsigned int i = 0; i < this->selectedUrls.size(); i++) {
                if (this->selectedUrls[i] == fileId) this->selectedUrls.erase(this->selectedUrls.begin() + i);
            }
            this->drawMenuItems(false);
        } else if (this->menu->GetItems()[selectedIndex]->GetIconTexture() ==
                   inst::util::loadTex("romfs:/images/icons/checkbox-blank-outline.png")) {
            this->selectedUrls.push_back(fileId);
            this->alternativeNames.push_back(this->ourUrls[selectedIndex - dirListSize].name);
            this->drawMenuItems(false);
        } else {
            try {
                this->infoImage->SetVisible(true);
                this->ourUrls = client->list(fileId);
                if (selectedIndex >= dirListSize) {
                    this->lastFileId.push_back(fileId);
                }
                this->drawMenuItems(true);
                this->menu->SetSelectedIndex(0);
                mainApp->CallForRender();
                this->infoImage->SetVisible(false);
                this->menu->SetVisible(true);
            } catch (...) {
                inst::ui::mainApp->CreateShowDialog("inst.net.failed"_lang, "", {"common.ok"_lang}, false);
            }
        }
    }

    void netInstPage::startNetwork() {
        std::vector<std::string> options = {"inst.net.src.opt0"_lang};
        std::string url;

        while (true) {
            this->butText->SetText("inst.net.buttons"_lang);
            this->menu->SetVisible(false);
            this->menu->ClearItems();
            this->infoImage->SetVisible(true);
            mainApp->LoadLayout(mainApp->netinstPage);
            this->ourUrls.clear();
            this->selectedUrls.clear();

            auto urls = netInstStuff::OnSelected();
            if (urls.empty()) {
                mainApp->LoadLayout(mainApp->mainPage);
                return;
            }
            if (urls[0] == "supplyUrl") {
                switch (
                    mainApp->CreateShowDialog("inst.net.src.title"_lang, "common.cancel_desc"_lang, options, false)) {
                case 0:
                    url = inst::util::softwareKeyboard("inst.net.url.hint"_lang, inst::config::lastNetUrl, 500);
                    if (url.empty()) continue;

                    client = drive::new_drive(drive::dt_httpdir);
                    this->ourUrls = client->list(url);
                    switch (this->ourUrls.size()) {
                    case 0:
                        inst::ui::mainApp->CreateShowDialog("inst.net.url.invalid"_lang, "", {"common.ok"_lang}, false);
                        continue;
                    case 1:
                        this->selectedUrls = {this->ourUrls[0].id};
                        this->startInstall(true);
                        return;
                    default:
                        this->lastFileId.push_back(url);
                    }
                    break;

                default:
                    continue;
                }
            } else {
                for (std::string url : urls) {
                    this->ourUrls.push_back({id : url, name : inst::util::formatUrlString(url)});
                }
            }

            mainApp->CallForRender();  // If we re-render a few times during this process the main screen won't flicker
            sourceString = "inst.net.source_string"_lang;
            this->pageInfoText->SetText("inst.net.top_info"_lang);
            this->butText->SetText("inst.net.buttons1"_lang);
            this->drawMenuItems(true);
            this->menu->SetSelectedIndex(0);
            mainApp->CallForRender();
            this->infoImage->SetVisible(false);
            this->menu->SetVisible(true);
            return;
        }
    }

    void netInstPage::startInstall(bool urlMode) {
        int dialogResult = -1;
        if (this->selectedUrls.size() == 1) {
            std::string ourUrlString;
            if (this->alternativeNames.size() > 0)
                ourUrlString = inst::util::shortenString(this->alternativeNames[0], 32, true);
            else
                ourUrlString = inst::util::shortenString(inst::util::formatUrlString(this->selectedUrls[0]), 32, true);
            dialogResult = mainApp->CreateShowDialog("inst.target.desc0"_lang + ourUrlString + "inst.target.desc1"_lang,
                "common.cancel_desc"_lang, {"inst.target.opt0"_lang, "inst.target.opt1"_lang}, false);
        } else
            dialogResult = mainApp->CreateShowDialog(
                "inst.target.desc00"_lang + std::to_string(this->selectedUrls.size()) + "inst.target.desc01"_lang,
                "common.cancel_desc"_lang, {"inst.target.opt0"_lang, "inst.target.opt1"_lang}, false);
        if (dialogResult == -1 && !urlMode)
            return;
        else if (dialogResult == -1 && urlMode) {
            this->startNetwork();
            return;
        }
        netInstStuff::installTitleNet(this->selectedUrls, dialogResult, this->alternativeNames, sourceString);
        return;
    }

    void netInstPage::onInput(u64 Down, u64 Up, u64 Held, pu::ui::TouchPoint Pos) {
        if (Down & HidNpadButton_B) {
            if (this->selectedUrls.size() > 0) {
                netInstStuff::sendExitCommands(inst::util::formatUrlLink(this->selectedUrls[0]));
            }
            netInstStuff::OnUnwound();
            mainApp->LoadLayout(mainApp->mainPage);
        }
        if ((Down & HidNpadButton_A) || (Up & pu::ui::TouchPseudoKey)) {
            this->selectTitle(this->menu->GetSelectedIndex());
            if (this->menu->GetItems().size() == 1 && this->selectedUrls.size() == 1) {
                this->startInstall(false);
            }
        }
        if ((Down & HidNpadButton_Y)) {
            if (this->selectedUrls.size() == this->menu->GetItems().size())
                this->drawMenuItems(true);
            else {
                for (size_t i = 0; i < this->menu->GetItems().size(); i++) {
                    if (this->menu->GetItems()[i]->GetIconTexture() == inst::util::loadTex("romfs:/images/icons/checkbox-blank-outline.png"))
                        this->selectTitle(i);
                }
                this->drawMenuItems(false);
            }
        }
        if (Down & HidNpadButton_Plus) {
            if (this->selectedUrls.size() > 0) {
                this->startInstall(false);
            }
        }
    }
}  // namespace inst::ui
