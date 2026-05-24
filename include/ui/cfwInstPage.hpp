#pragma once
#include <string>
#include <vector>
#include <pu/Plutonium>

using namespace pu::ui::elm;
namespace inst::ui {
    struct CfwComponent {
        std::string displayName;
        std::string version;          // latest version available on GitHub
        std::string url;
        std::string zipName;
        std::string postProcess;      // tag for per-component post-extract step ("", "emuiibo_sdout", "hekate_payload")
        std::string installedVersion; // populated from inst::config::cfwInstalled before showing the page; empty if never installed via Leaf
        bool defaultSelected = true;  // whether the row is checked when the selection page opens
    };

    class cfwInstPage : public pu::ui::Layout
    {
        public:
            cfwInstPage();
            PU_SMART_CTOR(cfwInstPage)
            pu::ui::elm::Menu::Ref menu;
            void setComponents(const std::vector<CfwComponent>& components);
            void onInput(u64 Down, u64 Up, u64 Held, pu::ui::TouchPoint Pos);
            TextBlock::Ref pageInfoText;
            Image::Ref titleImage;
            TextBlock::Ref appVersionText;
        private:
            std::vector<CfwComponent> components;
            std::vector<bool> selected;
            TextBlock::Ref butText;
            Rectangle::Ref topRect;
            Rectangle::Ref infoRect;
            Rectangle::Ref botRect;
            void redraw();
            void toggle(int index);
            void startInstall();
    };
}
