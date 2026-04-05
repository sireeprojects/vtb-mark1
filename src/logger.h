#pragma once

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace vtb {

enum class LogLevel { ERROR = 0, DEFAULT = 1, FULL = 2 };

class Logger {
public:
   static Logger& get_instance();

   void init(const std::string& filename, LogLevel level);
   LogLevel get_level() const;
   void log(LogLevel msg_level, const std::string& message);

   ~Logger();

private:
   Logger();
   Logger(const Logger&) = delete;
   Logger& operator=(const Logger&) = delete;

   void flush_loop();

   std::ofstream file_;
   std::stringstream buffer_;
   std::mutex mutex_;
   std::thread flush_thread_;

   LogLevel level_{LogLevel::DEFAULT};
   std::atomic<bool> running_{false};
   std::atomic<bool> initialized_{false};

   std::condition_variable cv_;
};

}  // namespace vtb
