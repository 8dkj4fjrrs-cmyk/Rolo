#include "rolo/core/Log.hpp"

#include <algorithm>
#include <iostream>
#include <utility>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace rolo {

std::string_view toString(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
    }
    return "UNKNOWN";
}

Logger::SinkId Logger::addSink(LogSink sink)
{
    const SinkId id = m_nextId++;
    m_sinks.push_back(Entry{id, std::move(sink)});
    return id;
}

void Logger::removeSink(SinkId id)
{
    std::erase_if(m_sinks, [id](const Entry& entry) { return entry.id == id; });
}

void Logger::log(LogLevel level, std::string_view category, std::string_view text) const
{
    if (level < m_minimumLevel) {
        return;
    }

    const LogMessage message{level, std::string(category), std::string(text)};
    for (const Entry& entry : m_sinks) {
        if (entry.sink) {
            entry.sink(message);
        }
    }
}

LogSink makeConsoleSink()
{
#ifdef __ANDROID__
    // An Android app has no console: what it prints goes nowhere. Its log is
    // "logcat" (seen in Android Studio, or with `adb logcat -s Rolo`), where
    // every line has a level and a tag; Rolo's tag is "Rolo".
    return [](const LogMessage& message) {
        int priority = ANDROID_LOG_INFO;
        switch (message.level) {
        case LogLevel::Trace: priority = ANDROID_LOG_VERBOSE; break;
        case LogLevel::Debug: priority = ANDROID_LOG_DEBUG; break;
        case LogLevel::Info: priority = ANDROID_LOG_INFO; break;
        case LogLevel::Warning: priority = ANDROID_LOG_WARN; break;
        case LogLevel::Error: priority = ANDROID_LOG_ERROR; break;
        }
        __android_log_print(priority, "Rolo", "%s: %s", message.category.c_str(), message.text.c_str());
    };
#else
    return [](const LogMessage& message) {
        std::ostream& out = (message.level >= LogLevel::Warning) ? std::cerr : std::cout;
        out << '[' << toString(message.level) << "] " << message.category << ": " << message.text << '\n';
    };
#endif
}

} // namespace rolo
