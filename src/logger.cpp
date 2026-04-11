#include <chrono>
#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <algorithm>
#include <string>
#include <string_view>

#include "logger.h"

namespace vtb {

Logger& Logger::get_instance() {
   static Logger instance;
   return instance;
}

Logger::Logger() : max_file_size_(100 * 1024 * 1024) {
}

void Logger::init(const std::string& filename, LogLevel level, size_t max_file_size_mb) {
   if (initialized_.exchange(true)) return;

   level_ = level;
   log_filename_ = filename;
   max_file_size_ = max_file_size_mb * 1024 * 1024;

   if (!log_filename_.empty()) {
      // overwrite log
      file_.open(log_filename_, std::ios::out);
      if (file_.is_open()) {
         current_file_size_ = std::filesystem::file_size(log_filename_);
      }
   }

   running_ = true;
   flush_thread_ = std::thread(&Logger::flush_loop, this);
}

void Logger::log(LogLevel msg_level, std::string&& message) {
   // Level check here is a secondary safety; the Macro handles the primary check
   if (msg_level <= level_) {
      std::lock_guard<std::mutex> lock(mutex_);
      active_buffer_.append(get_level_name(msg_level)).append(message).append("\n");
   }
}

void Logger::rotate_logs() {
   if (file_.is_open()) file_.close();

   // Simple rotation: app.log -> app.log.1 (can be expanded to keep N files)
   std::string backup_name = log_filename_ + ".1";
   std::filesystem::rename(log_filename_, backup_name);

   file_.open(log_filename_, std::ios::out | std::ios::trunc);
   current_file_size_ = 0;
}

void Logger::flush_loop() {
   while (running_) {
      std::string to_write;
      {
         std::unique_lock<std::mutex> lock(mutex_);
         cv_.wait_for(lock, std::chrono::milliseconds(100), [this] { return !running_ || !active_buffer_.empty(); });
         
         // SWAP Strategy: Move the data out of the shared buffer instantly
         to_write.swap(active_buffer_);
      }

      if (!to_write.empty()) {
         if (file_.is_open()) {
            if (current_file_size_ + to_write.size() > max_file_size_) {
               rotate_logs();
            }
            file_ << to_write << std::flush;
            current_file_size_ += to_write.size();
         }
         std::cout << to_write << std::flush;
      }
   }
}

Logger::~Logger() {
   running_ = false;
   cv_.notify_all();
   if (flush_thread_.joinable()) flush_thread_.join();
   
   // Final drain to ensure no logs are lost on shutdown
   if (!active_buffer_.empty()) {
      if (file_.is_open()) file_ << active_buffer_;
      std::cout << active_buffer_;
   }
}

// Function to return the uppercase name of the LogLevel
std::string_view Logger::get_level_name(LogLevel level) {
    switch (level) {
        case LogLevel::FATAL:   return "[..FATAL] *** ";
        case LogLevel::ERROR:   return "[..ERROR] *** ";
        case LogLevel::WARNING: return "[WARNING] *** ";
        case LogLevel::INFO:    return "[...INFO] ";
        case LogLevel::DEBUG:   return "[..DEBUG] ";
        case LogLevel::TRACE:   return "[..TRACE] ";
        default:                return "[UNKNOWN] ";
    }
}

void set_verbosity(std::string level_str) {
   // Convert to uppercase to handle "info", "Info", or "INFO"
   std::transform(level_str.begin(), level_str.end(), level_str.begin(), ::toupper);

   // Define the mapping
   static const std::unordered_map<std::string, LogLevel> verbosity_map = {
      {"FATAL",   LogLevel::FATAL},
      {"ERROR",   LogLevel::ERROR},
      {"WARNING", LogLevel::WARNING},
      {"INFO",    LogLevel::INFO},
      {"DEBUG",   LogLevel::DEBUG},
      {"TRACE",   LogLevel::TRACE}
   };

   // Search the map
   auto it = verbosity_map.find(level_str);
   if (it != verbosity_map.end()) {
      vtb::Logger::get_instance().set_level(it->second);
   } else {
      // Fallback for invalid input
      std::cerr << "[WARNING] Invalid log level '" << level_str << "'. Defaulting to INFO.\n";
   }
}

}  // namespace vtb

// word wrapping at spaces only
// void Logger::log(LogLevel msg_level, std::string&& message) {
//    if (msg_level <= level_) {
//       std::lock_guard<std::mutex> lock(mutex_);
// 
//       const size_t max_line_width = 80;
//       std::string_view level_name = get_level_name(msg_level);
//       size_t indent_size = level_name.length();
// 
//       active_buffer_.append(level_name);
// 
//       size_t start = 0;
//       while (start < message.length()) {
//          size_t remaining = message.length() - start;
// 
//          // If the rest of the message fits on one line, we are done
//          if (remaining <= max_line_width) {
//             active_buffer_.append(message.substr(start)).append("\n");
//             break;
//          }
// 
//          // Look for the last space within the max_line_width range
//          size_t end = start + max_line_width;
//          size_t last_space = message.find_last_of(' ', end);
// 
//          // Determine where to break
//          size_t break_at;
//          if (last_space != std::string::npos && last_space >= start) {
//             // Found a space: break there and skip the space itself for the next line
//             break_at = last_space;
//             active_buffer_.append(message.substr(start, break_at - start)).append("\n");
//             start = break_at + 1;
//          } else {
//             // No space found: force break at the limit (long word case)
//             break_at = end;
//             active_buffer_.append(message.substr(start, break_at - start)).append("\n");
//             start = break_at;
//          }
// 
//          // If more text remains, add indentation to align with the first line
//          if (start < message.length()) {
//             active_buffer_.append(indent_size, ' ');
//          }
//       }
//    }
// }
