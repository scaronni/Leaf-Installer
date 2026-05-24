#pragma once
#include <string>

namespace inst::zip {
    // Extracts `filename` (a zip file) into `destination`. If `errorOut` is
    // provided, it is populated with a short diagnostic on failure: the entry
    // path that couldn't be written, the minizip error code, and (when known)
    // errno or the failing stage.
    bool extractFile(const std::string filename, const std::string destination, std::string* errorOut = nullptr);
}
