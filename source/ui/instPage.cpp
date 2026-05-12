#include <filesystem>
#include "ui/MainApplication.hpp"
#include "ui/instPage.hpp"
#include "util/config.hpp"
#include "util/util.hpp"

#define COLOR(hex) pu::ui::Color::FromHex(hex)

namespace inst::ui {
    extern MainApplication *mainApp;

    instPage::instPage() : Layout::Layout() {
        this->SetBackgroundColor(COLOR("#670000FF"));
        this->Add(inst::util::makeBackgroundImage());
        this->topRect = Rectangle::New(0, 0, 1920, 141, COLOR("#170909FF"));
        this->infoRect = Rectangle::New(0, 142, 1920, 90, COLOR("#17090980"));
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
        this->fileNameText = TextBlock::New(22, 250, "");
        this->fileNameText->SetFont(pu::ui::MakeDefaultFontName(33));
        this->fileNameText->SetColor(COLOR("#FFFFFFFF"));
        this->installInfoText = TextBlock::New(22, 852, "");
        this->installInfoText->SetFont(pu::ui::MakeDefaultFontName(33));
        this->installInfoText->SetColor(COLOR("#FFFFFFFF"));
        this->installBar = pu::ui::elm::ProgressBar::New(15, 900, 1275, 60, 100.0f);
        this->installBar->SetBackgroundColor(COLOR("#222222FF"));
        if (std::filesystem::exists(inst::config::appDir + "/awoo_inst.png")) this->awooImage = Image::New(615, 285, inst::util::loadTex(inst::config::appDir + "/awoo_inst.png"));
        else this->awooImage = Image::New(765, 249, inst::util::loadTex("romfs:/images/awoos/7d8a05cddfef6da4901b20d2698d5a71.png"));
        this->awooImage->SetWidth(1146);
        this->awooImage->SetHeight(831);
        this->Add(this->topRect);
        this->Add(this->infoRect);
        this->Add(this->titleImage);
        this->Add(this->appVersionText);
        inst::util::addStorageInfoBlocks(this, 1900, 25);
        this->Add(this->pageInfoText);
        this->Add(this->fileNameText);
        this->Add(this->installInfoText);
        this->Add(this->installBar);
        this->Add(this->awooImage);
        if (inst::config::gayMode) this->awooImage->SetVisible(false);
    }

    void instPage::setTopInstInfoText(std::string ourText){
        mainApp->instpage->pageInfoText->SetText(ourText);
        mainApp->CallForRender();
    }

    void instPage::setFileNameText(std::string ourText){
        mainApp->instpage->fileNameText->SetText(ourText);
        mainApp->CallForRender();
    }

    // Updates both header texts with a single CallForRender. Do not split this into
    // setTopInstInfoText + setFileNameText at install-loop sites: the second render
    // between selecting a file and the install task's Prepare() desyncs NS-USBloader
    // and BufferData hits its 5s read timeout. One render per iteration is required.
    void instPage::setTopInfo(std::string topText, std::string fileName){
        mainApp->instpage->pageInfoText->SetText(topText);
        mainApp->instpage->fileNameText->SetText(fileName);
        mainApp->CallForRender();
    }

    void instPage::setInstInfoText(std::string ourText){
        mainApp->instpage->installInfoText->SetText(ourText);
        mainApp->CallForRender();
    }

    void instPage::setInstBarPerc(double ourPercent){
        mainApp->instpage->installBar->SetVisible(true);
        mainApp->instpage->installBar->SetProgress(ourPercent);
        mainApp->CallForRender();
    }

    void instPage::loadMainMenu(){
        mainApp->LoadLayout(mainApp->mainPage);
    }

    void instPage::loadInstallScreen(){
        mainApp->instpage->pageInfoText->SetText("");
        mainApp->instpage->fileNameText->SetText("");
        mainApp->instpage->installInfoText->SetText("");
        mainApp->instpage->installBar->SetProgress(0);
        mainApp->instpage->installBar->SetVisible(false);
        mainApp->instpage->awooImage->SetVisible(!inst::config::gayMode);
        mainApp->LoadLayout(mainApp->instpage);
        mainApp->CallForRender();
    }

    void instPage::onInput(u64 Down, u64 Up, u64 Held, pu::ui::TouchPoint Pos) {
    }
}
