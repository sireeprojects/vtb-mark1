#pragma once

#include <sstream>
#include <string>
#include "logger.h"

namespace vtb {

class LogStream {
public:
   LogStream(LogLevel level, const std::string& prefix) 
       : level_(level), prefix_(prefix) {}
   
   // Transfers the string to the logger on destruction
   ~LogStream() {
      // Use std::move to transfer the string buffer to the logger's async queue.
      // This prevents a full copy of the string data.
      Logger::get_instance().log(level_, std::move(oss_.str()));
   }

   template <typename T>
   LogStream& operator<<(const T& value) {
      oss_ << value;
      return *this;
   }

private:
   LogLevel level_;
   std::string prefix_;
   std::ostringstream oss_;
};

void set_verbosity(std::string level_str);

} // namespace vtb

// THE UNIFIED MACRO
// Checks the level before even creating the LogStream object.
#define VTB_LOG(level) \
    if (vtb::Logger::get_instance().get_level() >= vtb::LogLevel::level) \
        vtb::LogStream(vtb::LogLevel::level, "[" #level "] ")
