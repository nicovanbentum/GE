#pragma once

#include "pch.h"

namespace Raekor {

class Application {

public:
    Application() {}

    void run();
    void vulkan_main();

    template<class C>
    void serialize(C& archive) {
        archive(
            CEREAL_NVP(name),
            CEREAL_NVP(display),
            CEREAL_NVP(font),
            CEREAL_NVP(skyboxes));
    }
    void serialize_settings(const std::string& filepath, bool write = false);

    static bool running;
    static bool showUI;

private:
    std::string name;
    std::string font;
    int display;
    std::map<std::string, std::array<std::string, 6>> skyboxes;
};

} // Namespace Raekor