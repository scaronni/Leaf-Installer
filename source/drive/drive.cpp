#include <stdexcept>
#include "util/drive.hpp"
#include "httpdir.hpp"

namespace inst::drive {
    drive::ref new_drive(drive_type type) {
        switch (type) {
        case dt_httpdir:
            return std::make_shared<httpdir>();
        default:
            throw std::runtime_error("unsupport drive type");
        }
    }

}  // namespace inst::drive