#pragma once
#include <string>
#include <vector>
#include "ui/cfwInstPage.hpp"

namespace cfw {
    void startCfwInstall();
    void installSelectedComponents(const std::vector<inst::ui::CfwComponent>& components);
}
