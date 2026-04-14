#include "common.h"

#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>

#ifdef __GNUC__
#include <cxxabi.h>
#endif


// #include "port_controller_back2back.h"
// #include "port_controller_emulator.h"
#include "port_controller_loopback.h"

namespace vtb {

std::unique_ptr<PortController> create_controller(std::string_view mode) {
   if (mode == "Loopback") {
      return std::make_unique<PortControllerLoopback>();
   }
   if (mode == "Back2Back") { // TODO
      // return std::make_unique<PortControllerBack2Back>();
   }
   if (mode == "Emulator") { // TODO
      // return std::make_unique<PortControllerEmulator>();
   }
   // TODO add error message here
   return nullptr;
}

int create_client_socket(const std::string& path) {
   if (path.empty()) return -1;

   // 1. Create the socket
   int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
   if (sock_fd < 0) {
      perror("socket");
      return -1;
   }

   struct sockaddr_un addr;
   std::memset(&addr, 0, sizeof(addr));
   addr.sun_family = AF_UNIX;

   size_t path_len = path.length();

   // Check if the maximum path limit is exceeded
   if (path_len >= sizeof(addr.sun_path)) {
      close(sock_fd);
      return -1;
   }
   if (path[0] == '\0') {
      // Abstract socket: first byte must be '\0'
      addr.sun_path[0] = '\0';
      std::memcpy(&addr.sun_path[1], path.c_str() + 1, path_len - 1);

      // Length must include the null byte and the name length
      // Size: offset of sun_path + 1 (null) + name_length
      socklen_t len = offsetof(struct sockaddr_un, sun_path) + path_len;

      if (connect(sock_fd, (struct sockaddr*)&addr, len) == -1) {
         VTB_LOG(ERROR) << "VtbCommon: Abstract Socket Connect Failed";
         sock_fd = -1;
      }
   } else {
      // Standard UNIX socket: Path on filesystem
      std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

      // Remove existing file if it exists (standard cleanup for AF_UNIX)
      unlink(path.c_str());

      if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
         VTB_LOG(ERROR) << "VtbCommon: Unix Socket Connect Failed";
         sock_fd = -1;
      }
   }
   return sock_fd;
}

/**
 * Creates and binds/prepares a UNIX or Abstract socket.
 * @param path The filesystem path (e.g., "/tmp/sock") or
 * abstract name (e.g., "@my_socket").
 * @return The socket file descriptor (socket id).
 */
int create_server_socket(const std::string& path) {
   if (path.empty()) return -1;

   // 1. Create the socket
   int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
   if (sock_fd < 0) {
      perror("socket");
      return -1;
   }

   struct sockaddr_un addr;
   std::memset(&addr, 0, sizeof(addr));
   addr.sun_family = AF_UNIX;

   size_t path_len = path.length();

   // Check if the maximum path limit is exceeded
   if (path_len >= sizeof(addr.sun_path)) {
      close(sock_fd);
      return -1;
   }

   // 2. Handle Abstract vs. Path-based
   // Convention: Using '@' to denote an abstract socket in the input string
   if (path[0] == '\0') {
      // Abstract socket: first byte must be '\0'
      addr.sun_path[0] = '\0';
      std::memcpy(&addr.sun_path[1], path.c_str() + 1, path_len - 1);

      // Length must include the null byte and the name length
      // Size: offset of sun_path + 1 (null) + name_length
      socklen_t len = offsetof(struct sockaddr_un, sun_path) + path_len;

      if (bind(sock_fd, (struct sockaddr*)&addr, len) < 0) {
         perror("bind abstract");
         close(sock_fd);
         return -1;
      }
   } else {
      // Standard UNIX socket: Path on filesystem
      std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

      // Remove existing file if it exists (standard cleanup for AF_UNIX)
      unlink(path.c_str());

      if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
         perror("bind unix");
         close(sock_fd);
         return -1;
      }
   }
   return sock_fd;
}

void set_thread_name(std::thread& th, const std::string& name) {
   // We need the native handle (which is a pthread_t on Linux)
   auto handle = th.native_handle();

   // pthread_setname_np returns 0 on success
   int rc = pthread_setname_np(handle, name.c_str());

   if (rc != 0) {
      std::cerr << "Error setting thread name: " 
                << std::strerror(rc)
                << std::endl;
   }
}

struct termios old_t, new_t;

void disable_echoctl() {
   // Get current terminal settings
   tcgetattr(STDIN_FILENO, &old_t);
   new_t = old_t;

   // Hide the ^C (Disable ECHOCTL)
   new_t.c_lflag &= ~ECHOCTL;
   tcsetattr(STDIN_FILENO, TCSANOW, &new_t);
}

void restore_echoctl() {
   tcsetattr(STDIN_FILENO, TCSANOW, &old_t);
}

void graceful_exit() {
    VTB_LOG(FATAL) << "A fatal error occurred. Shutting down gracefully...";
    // std::exit(1);
}

bool is_even(int n) {
   return (n % 2 == 0);
}

bool is_odd(int n) {
   return (n % 2 != 0);
}

std::string format_qids(const std::vector<int>& vec) {
   std::stringstream ss;
   ss << "{";
   for (size_t i = 0; i < vec.size(); ++i) {
      ss << vec[i] << (i == vec.size() - 1 ? "" : ", ");
   }
   ss << "}";
   return ss.str();
}

std::string demangle(const char* name) {
#ifdef __GNUC__
   int status;
   char* res = abi::__cxa_demangle(name, nullptr, nullptr, &status);
   if (status == 0) {
      std::string s(res);
      free(res);
      return s;
   }
#endif
   return name; // Fallback to mangled name if not on GCC/Clang
}

ThreadMode string_to_thread_mode(std::string_view mode_str) {
   if (mode_str == "EachQ-TwoThread") {
      return ThreadMode::EachQTwoThread;
   }
   if (mode_str == "EachQ-OneThread") {
      return ThreadMode::EachQOneThread;
   }
   if (mode_str == "AllQ-TwoThread") {
      return ThreadMode::AllQTwoThread;
   }
   if (mode_str == "AllQ-OneThread") {
      return ThreadMode::AllQOneThread;
   }

   throw std::invalid_argument("Invalid thread mode string: " + std::string(mode_str));
}


}  // namespace vtb
