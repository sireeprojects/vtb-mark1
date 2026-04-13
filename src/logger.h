#pragma once

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <iostream>

namespace vtb {

enum class LogLevel {
   FATAL = 0,
   ERROR = 1,
   WARNING = 2,
   INFO = 3,
   DEBUG = 4,
   TRACE = 5
};

class Logger {
public:
   static Logger& get_instance();

   void init(const std::string& filename, LogLevel level, size_t max_file_size_mb = 100);
   LogLevel get_level() const { return level_; }

   void set_level(LogLevel msg_level) {
      level_ = msg_level;
   }
   
   // Optimized to take string by rvalue to avoid deep copies
   void log(LogLevel msg_level, std::string&& message);

   void emergency_flush();
   void direct_append(const std::string& msg);

   ~Logger();

private:
   Logger();
   Logger(const Logger&) = delete;
   Logger& operator=(const Logger&) = delete;

   void flush_loop();
   void rotate_logs();
   std::string_view get_level_name(LogLevel level);

   std::string log_filename_;
   std::ofstream file_;
   
   // Double buffering: active_buffer_ gathers logs while flush_thread_ writes the other
   std::string active_buffer_; 
   std::mutex mutex_;
   std::thread flush_thread_;
   std::condition_variable cv_;

   LogLevel level_{LogLevel::INFO};
   size_t max_file_size_;
   size_t current_file_size_{0};
   
   std::atomic<bool> running_{false};
   std::atomic<bool> initialized_{false};
};

}  // namespace vtb
