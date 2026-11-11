
#pragma once

#include <string_view>

namespace Coldplay::Protocol {

    inline constexpr int VERSION = 1;

    inline constexpr std::string_view APP_NAME = "COLDPLAY";

    // Desktop <-> Geode message types
    inline constexpr std::string_view HELLO = "hello";
    inline constexpr std::string_view STATUS = "status";
    inline constexpr std::string_view LOG = "log";
    inline constexpr std::string_view SET_OPTION = "set_option";
    inline constexpr std::string_view PING = "ping";
    inline constexpr std::string_view PONG = "pong";

}
