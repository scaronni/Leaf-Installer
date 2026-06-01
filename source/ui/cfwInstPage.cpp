#include "ui/MainApplication.hpp"
#include "ui/mainPage.hpp"
#include "ui/cfwInstPage.hpp"
#include "cfwInstall.hpp"
#include "util/util.hpp"
#include "util/config.hpp"
#include "util/lang.hpp"

#define COLOR(hex) pu::ui::Color::FromHex(hex)

namespace inst::ui {
    extern MainApplication *mainApp;

    cfwInstPage::cfwInstPage() : Layout::Layout() {
        this->SetBackgroundColor(COLOR("#670000FF"));
        this->Add(inst::util::makeBackgroundImage());
        this->topRect = Rectangle::New(0, 0, 1920, 141, COLOR("#170909FF"));
        this->infoRect = Rectangle::New(0, 142, 1920, 90, COLOR("#17090980"));
        this->botRect = Rectangle::New(0, 990, 1920, 90, COLOR("#17090980"));
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
        this->pageInfoText = TextBlock::New(15, 164, "cfw.select_top_info"_lang);
        this->pageInfoText->SetFont(pu::ui::MakeDefaultFontName(45));
        this->pageInfoText->SetColor(COLOR("#FFFFFFFF"));
        this->butText = TextBlock::New(15, 1017, "cfw.select_buttons"_lang);
        this->butText->SetFont(pu::ui::MakeDefaultFontName(36));
        this->butText->SetColor(COLOR("#FFFFFFFF"));
        this->menu = pu::ui::elm::Menu::New(0, 234, 1920, COLOR("#FFFFFF00"), COLOR("#FFFFFF00"), 108, 7);
        this->menu->SetItemsFocusColor(COLOR("#170909C0"));
        this->menu->SetScrollbarColor(COLOR("#17090980"));
        this->Add(this->topRect);
        this->Add(this->infoRect);
        this->Add(this->botRect);
        this->Add(this->titleImage);
        this->Add(this->appVersionText);
        inst::util::addStorageInfoBlocks(this, 1900, 25);
        this->Add(this->butText);
        this->Add(this->pageInfoText);
        this->Add(this->menu);
    }

    void cfwInstPage::setComponents(const std::vector<CfwComponent>& comps) {
        this->components = comps;
        this->selected.resize(comps.size());
        for (size_t i = 0; i < comps.size(); i++) {
            // A component is selected by default unless it's already installed
            // at the exact available version. Never installed → checked.
            // Installed at older/different version → checked. Same version
            // (e.g. 2.4.2 → 2.4.2) → unchecked so the user doesn't waste time
            // re-downloading and re-staging the same files.
            const bool up_to_date = !comps[i].installedVersion.empty()
                                 && comps[i].installedVersion == comps[i].version;
            this->selected[i] = comps[i].defaultSelected && !up_to_date;
        }
        this->menu->SetSelectedIndex(0);
        this->redraw();
    }

    void cfwInstPage::redraw() {
        this->menu->ClearItems();
        for (size_t i = 0; i < this->components.size(); i++) {
            const auto& c = this->components[i];
            std::string versionText = c.version;
            if (!c.installedVersion.empty()) {
                versionText = c.installedVersion + "  →  " + c.version;
            }
            auto entry = pu::ui::elm::MenuItem::New(c.displayName + "    " + versionText);
            entry->SetColor(COLOR("#FFFFFFFF"));
            if (this->selected[i]) {
                entry->SetIcon(inst::util::loadTex("romfs:/images/icons/check-box-outline.png"));
            } else {
                entry->SetIcon(inst::util::loadTex("romfs:/images/icons/checkbox-blank-outline.png"));
            }
            // Touch tap toggles the row. Plutonium has already moved the
            // highlight to the tapped row by the time this fires, so the
            // index passed to `toggle` is correct.
            const int captured = static_cast<int>(i);
            entry->AddOnKey([this, captured]() { this->toggle(captured); }, pu::ui::TouchPseudoKey);
            this->menu->AddItem(entry);
        }
    }

    void cfwInstPage::toggle(int index) {
        if (index < 0 || index >= (int)this->components.size()) return;
        this->selected[index] = !this->selected[index];
        int previous = this->menu->GetSelectedIndex();
        this->redraw();
        this->menu->SetSelectedIndex(previous);
    }

    void cfwInstPage::startInstall() {
        std::vector<CfwComponent> toInstall;
        for (size_t i = 0; i < this->components.size(); i++) {
            if (this->selected[i]) toInstall.push_back(this->components[i]);
        }
        if (toInstall.empty()) {
            mainApp->CreateShowDialog("cfw.no_selection_title"_lang, "cfw.no_selection_desc"_lang, {"common.ok"_lang}, true);
            return;
        }
        cfw::installSelectedComponents(toInstall);
    }

    void cfwInstPage::onInput(u64 Down, u64 Up, u64 Held, pu::ui::TouchPoint Pos) {
        if (Down & HidNpadButton_B) {
            mainApp->LoadLayout(mainApp->mainPage);
        }
        if ((Down & HidNpadButton_A) || (Up & pu::ui::TouchPseudoKey)) {
            this->toggle(this->menu->GetSelectedIndex());
        }
        if (Down & HidNpadButton_Y) {
            bool allSelected = !this->selected.empty();
            for (bool s : this->selected) {
                if (!s) { allSelected = false; break; }
            }
            for (size_t i = 0; i < this->selected.size(); i++) {
                this->selected[i] = !allSelected;
            }
            int previous = this->menu->GetSelectedIndex();
            this->redraw();
            this->menu->SetSelectedIndex(previous);
        }
        if (Down & HidNpadButton_Plus) {
            this->startInstall();
        }
    }
}
