#pragma once
#include <pu/Plutonium>

using namespace pu::ui::elm;
namespace inst::ui {
    class instPage : public pu::ui::Layout
    {
        public:
            instPage();
            PU_SMART_CTOR(instPage)
            void onInput(u64 Down, u64 Up, u64 Held, pu::ui::TouchPoint Pos);
            TextBlock::Ref pageInfoText;
            TextBlock::Ref fileNameText;
            TextBlock::Ref installInfoText;
            pu::ui::elm::ProgressBar::Ref installBar;
            Image::Ref awooImage;
            Image::Ref titleImage;
            TextBlock::Ref appVersionText;
            static void setTopInstInfoText(std::string ourText);
            static void setFileNameText(std::string ourText);
            static void setTopInfo(std::string topText, std::string fileName);
            static void setInstInfoText(std::string ourText);
            static void setInstBarPerc(double ourPercent);
            static void loadMainMenu();
            static void loadInstallScreen();
        private:
            Rectangle::Ref infoRect;
            Rectangle::Ref topRect;
    };
}