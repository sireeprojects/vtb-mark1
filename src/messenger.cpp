#include "messenger.h"

namespace vtb {

LogStream::LogStream(LogLevel level, const std::string& prefix)
    : level_(level), prefix_(prefix) {}

LogStream::~LogStream() {
   // Only log if the global level allows it
   if (level_ <= Logger::get_instance().get_level()) {
      Logger::get_instance().log(level_, prefix_ + oss_.str());
   }
}

LogStream info() { return LogStream(LogLevel::DEFAULT, "[INFO] "); }

LogStream error() { return LogStream(LogLevel::DEFAULT, "[ERROR] "); }

LogStream details() { return LogStream(LogLevel::FULL, "[DETAILS] "); }

}  // namespace vtb
