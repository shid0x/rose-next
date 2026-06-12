#ifndef CLOG_H
#define CLOG_H
#pragma once

#include "rose/common/common_interface.h"

#include "fmt/format.h"
#include "fmt/printf.h"

#define LOG(level, msg, ...) \
    g_LOG.log(level, __FILE__, __LINE__, msg, __VA_ARGS__)

#define LOG_TRACE(msg, ...) \
    g_LOG.log(Rose::Common::LogLevel::Trace, __FILE__, __LINE__, msg, __VA_ARGS__)

#define LOG_DEBUG(msg, ...) \
    g_LOG.log(Rose::Common::LogLevel::Debug, __FILE__, __LINE__, msg, __VA_ARGS__)

#define LOG_INFO(msg, ...) \
    g_LOG.log(Rose::Common::LogLevel::Info, __FILE__, __LINE__, msg, __VA_ARGS__)

#define LOG_WARN(msg, ...) \
    g_LOG.log(Rose::Common::LogLevel::Warn, __FILE__, __LINE__, msg, __VA_ARGS__)

#define LOG_ERROR(msg, ...) \
    g_LOG.log(Rose::Common::LogLevel::Error, __FILE__, __LINE__, msg, __VA_ARGS__)

// -- Legacy defines for OutputString/CS_ODS
#define LOG_NORMAL 0x01
#define LOG_DEBUG_ 0x04
// -- End legacy defines

#define LOG_BUFFER_SIZE 1024

class Log;
extern Log g_LOG;

class Log {
public:
    // -- Legacy logging functions (printf-style format, e.g. %d %s) --
    template<typename... Args>
    void static OutputString(unsigned short wLogMODE, char* msg, Args&&... args) {
        legacy_printf(Rose::Common::LogLevel::Debug, msg, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void CS_ODS(unsigned short wLogMODE, char* msg, Args&&... args) {
        legacy_printf(Rose::Common::LogLevel::Debug, msg, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void CS_ODS(unsigned short log_mode, const char* msg, Args&&... args) {
        legacy_printf(Rose::Common::LogLevel::Debug, msg, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void static legacy_printf(Rose::Common::LogLevel level, const char* format_str, Args&&... args) {
        // fmt's printf parser is strict and THROWS fmt::format_error on the legacy
        // (often EUC-KR) %-format strings the original code fed to C sprintf, which
        // tolerated them. A logging call must never abort the process, so degrade to
        // the raw format text instead of letting the exception escape to terminate().
        std::string msg;
        try {
            msg = fmt::sprintf(format_str, args...);
        } catch (const fmt::format_error&) {
            msg = format_str ? format_str : "";
        }
        Rose::Common::logger_write(level, "", 0, msg.c_str());
    }
    // -- End Legacy

    // New logging facade
    template<typename... Args>
    void log(Rose::Common::LogLevel level,
        const char* file,
        uint32_t line,
        const char* format_str,
        Args&&... args) {

        // Same protection as legacy_printf: a malformed format string must not
        // crash the process via an uncaught fmt::format_error.
        std::string msg;
        try {
            msg = fmt::format(format_str, args...);
        } catch (const fmt::format_error&) {
            msg = format_str ? format_str : "";
        }
        Rose::Common::logger_write(level, file, line, msg.c_str());
    }
};

#define LogString g_LOG.OutputString

#endif // CLOG_H
