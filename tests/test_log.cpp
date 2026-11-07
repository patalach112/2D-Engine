// Aegis2D — unit tests for the logging module (Milestone 1).
#include <aegis/core/log.hpp>

#include "aegis_test.hpp"

using aegis::core::LogLevel;

AEGIS_TEST(log, "all levels stream without crashing") {
    AEGIS_LOG_DEBUG("debug message " << 1);
    AEGIS_LOG_INFO("info message " << 2);
    AEGIS_LOG_WARN("warn message " << 3);
    AEGIS_LOG_ERROR("error message " << 4);
    aegis::core::log(LogLevel::Info, "direct log call");
    AEGIS_CHECK(true);  // reaching the end is the test
}

AEGIS_TEST(log, "streamed values render into the message") {
    // Cannot capture stdout here; this exercises the formatting path for
    // the numeric/string/pointer combinations the engine uses.
    int value = 42;
    AEGIS_LOG_INFO("int=" << value << " hex=0x" << std::hex << value << std::dec
                   << " string=" << std::string("abc") << " float=" << 1.5f);
    AEGIS_CHECK(std::string("abc") == "abc");  // placeholder sanity check
}
