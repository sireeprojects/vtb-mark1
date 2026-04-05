#pragma once

#include <sstream>
#include <string>

#include "logger.h"

namespace vtb {

class LogStream {
public:
   LogStream(LogLevel level, const std::string& prefix);
   ~LogStream();

   // Support for all standard types
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

// Global access functions
LogStream info();
LogStream error();
LogStream details();

}  // namespace vtb
