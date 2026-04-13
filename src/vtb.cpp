#include <csignal>

#include "common.h"
#include "logger.h"
#include "messenger.h"
#include "cmdline_parser.h"
#include "config_manager.h"
#include "vhost_controller.h"
#include "port_controller_loopback.h"
#include <rte_launch.h>
#include <rte_pause.h>
#include <execinfo.h>
#include <unistd.h>

#ifdef __GNUC__
#include <cxxabi.h>
#endif

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

bool keep_running{false};
static int keep_alive_thread(void*);
static void signal_handler(int);
static void setup_signal_handler();

int main(int argc, char** argv) {
   vtb::disable_echoctl();

   setup_signal_handler();
 
   // setup logger with a default verbosity
   vtb::Logger::get_instance().init("run.log", vtb::LogLevel::TRACE);

   // setup configuration manager
   auto& config = vtb::ConfigManager::get_instance();
   if (!config.init(argc, argv)) {
      VTB_LOG(ERROR) << "Config.Init failed";
      return -1;
   }

   // set user verbosity, this overrides the default verbosity
   auto verbosity = config.get_arg<std::string>("--verbosity");
   vtb::set_verbosity(verbosity);

   // start the appropriate port controller
   auto mode = config.get_arg<std::string>("--mode");
   std::unique_ptr<vtb::PortController> port_controller = vtb::create_controller(mode);
   port_controller->start();

   // start vhost controller
   auto socket_path = config.get_arg<std::string>("--vhost-sockname");
   vtb::VhostController backend(socket_path.c_str());
   backend.init(argc, argv);
   backend.start();

   unsigned int next_core = rte_get_next_lcore(rte_get_main_lcore(), 1, 0);
   rte_eal_remote_launch(keep_alive_thread, NULL, next_core);

   rte_eal_mp_wait_lcore();

   vtb::restore_echoctl();
   VTB_LOG(INFO) << "Test Done. Starting cleanup...";
   return 0;
}

static void signal_handler(int sig) {
   // Disable handlers to prevent loops
   std::signal(SIGSEGV, SIG_DFL);
   std::signal(SIGABRT, SIG_DFL);

   std::stringstream ss;
   ss << "\n" << std::string(50, '=') << "\n";
   ss << "CRASH SIGNAL: " << sig << (sig == 6 ? " (SIGABRT/Terminate)" : " (SIGSEGV)") << "\n";

   void* array[20];
   size_t size = backtrace(array, 20);
   char** symbols = backtrace_symbols(array, size);

   for (size_t i = 0; i < size; i++) {
      std::string line(symbols[i]);
      std::string name = line;

      // Robust parsing: extract text between '(' and '+' OR after last ' '
      size_t begin = line.find_first_of('(');
      size_t end = line.find_first_of('+', begin);

      if (begin != std::string::npos && end != std::string::npos) {
         std::string mangled = line.substr(begin + 1, end - begin - 1);
         int status;
         char* demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
         if (status == 0) {
            name = demangled;
            free(demangled);
         } else {
            name = mangled;
         }
      }

      ss << "  [" << i << "] " << name << "\n";
   }
   free(symbols);
   ss << std::string(50, '=') << "\n";

   // Log and Flush
   vtb::Logger::get_instance().direct_append(ss.str());
   vtb::Logger::get_instance().emergency_flush();

   // To stop 'make' from printing "Aborted (core dumped)"
   // we exit with 0. Note: This prevents core file generation!
   // If you want a core dump, use std::raise(sig) instead of _exit(0).
   fprintf(stderr, "\n[VTB] Emergency shutdown complete. Exiting.\n");
   _exit(0);
}

static int keep_alive_thread(void*) {
   while (!keep_running) {
      rte_pause();
   }
   return 0;
}

static void setup_signal_handler() {
   std::set_terminate(vtb::graceful_exit);
   std::signal(SIGINT, signal_handler);  // detect ctrl+c
   std::signal(SIGTERM, signal_handler); // terminate from os
   std::signal(SIGSEGV, signal_handler); // Segfault
   std::signal(SIGABRT, signal_handler); // Abort (rte_panic)
   std::signal(SIGFPE,  signal_handler); // Math error
}                                         

/*
static void signal_handler(int sig) {
   VTB_LOG(FATAL) << "Process interrupted by user (Ctrl+C)";

   const char* msg = "\n--- CRASH DETECTED --- Signal: ";
   write(STDERR_FILENO, msg, strlen(msg));

   if (sig == SIGSEGV) write(STDERR_FILENO, "SIGSEGV (Check null pointers)\n", 30);
   if (sig == SIGABRT) write(STDERR_FILENO, "SIGABRT (Check rte_panic/assert)\n", 33);

   vtb::Logger::get_instance().emergency_flush();
   keep_running = true;
   // Re-raise the signal so a core dump is still generated
   std::signal(sig, SIG_DFL);
   std::raise(sig);
   _exit(0);
}


static void signal_handler(int sig) {
   VTB_LOG(FATAL) << "Process interrupted by user (Ctrl+C)";

   const char* msg = "\n--- CRASH DETECTED --- Signal: ";
   write(STDERR_FILENO, msg, strlen(msg));

   if (sig == SIGSEGV) write(STDERR_FILENO, "SIGSEGV (Check null pointers)\n", 30);
   if (sig == SIGABRT) write(STDERR_FILENO, "SIGABRT (Check rte_panic/assert)\n", 33);

   // 2. Capture the stack trace
   void* array[20];
   size_t size = backtrace(array, 20);

   // 3. Print the trace to stderr
   fprintf(stderr, "Stack Trace:\n");
   backtrace_symbols_fd(array, size, STDERR_FILENO);


   vtb::Logger::get_instance().emergency_flush();
   keep_running = true;
   // Re-raise the signal so a core dump is still generated
   std::signal(sig, SIG_DFL);
   std::raise(sig);
   _exit(0);
}

static void signal_handler(int sig) {
   VTB_LOG(FATAL) << "Process interrupted by user (Ctrl+C)";

   const char* msg = "\n--- CRASH DETECTED --- Signal: ";
   write(STDERR_FILENO, msg, strlen(msg));

   if (sig == SIGSEGV) write(STDERR_FILENO, "SIGSEGV (Check null pointers)\n", 30);
   if (sig == SIGABRT) write(STDERR_FILENO, "SIGABRT (Check rte_panic/assert)\n", 33);

   void* array[20];
   size_t size = backtrace(array, 20);
   char** symbols = backtrace_symbols(array, size);

   fprintf(stderr, "Stack Trace:\n");
   for (size_t i = 0; i < size; i++) {
      std::string sym(symbols[i]);

      // Find the parentheses where the mangled name is stored
      size_t open_paren = sym.find('(');
      size_t plus_sign = sym.find('+', open_paren);

      if (open_paren != std::string::npos && plus_sign != std::string::npos) {
         std::string mangled = sym.substr(open_paren + 1, plus_sign - open_paren - 1);

         int status;
         char* demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);

         if (status == 0) {
            fprintf(stderr, "  [%zu] %s\n", i, demangled);
            free(demangled);
         } else {
            fprintf(stderr, "  [%zu] %s\n", i, mangled.c_str());
         }
      } else {
         fprintf(stderr, "  [%zu] %s\n", i, symbols[i]);
      }
   }
   free(symbols);


   vtb::Logger::get_instance().emergency_flush();
   keep_running = true;
   // Re-raise the signal so a core dump is still generated
   std::signal(sig, SIG_DFL);
   std::raise(sig);
   _exit(0);
}


*/


/*

static void signal_handler(int sig) {
   // 1. Initial notification
   std::string sig_name = (sig == SIGSEGV) ? "SIGSEGV (Segmentation Fault)" :
                          (sig == SIGABRT) ? "SIGABRT (Abort/Panic)" : "Signal " + std::to_string(sig);

   VTB_LOG(FATAL) << "--- CRASH DETECTED --- " << sig_name;

   // 2. Capture Stack Trace
   void* array[20];
   size_t size = backtrace(array, 20);
   char** symbols = backtrace_symbols(array, size);

   if (symbols) {
      std::stringstream ss;
      ss << "Stack Trace Traceback:\n";

      for (size_t i = 0; i < size; i++) {
         std::string sym(symbols[i]);
         size_t open_paren = sym.find('(');
         size_t plus_sign = sym.find('+', open_paren);

         if (open_paren != std::string::npos && plus_sign != std::string::npos) {
            std::string mangled = sym.substr(open_paren + 1, plus_sign - open_paren - 1);
            int status;
            char* demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);

            if (status == 0) {
               ss << "  [" << i << "] " << demangled << "\n";
               free(demangled);
            } else {
               ss << "  [" << i << "] " << mangled << "\n";
            }
         } else {
            ss << "  [" << i << "] " << symbols[i] << "\n";
         }
      }
      free(symbols);

      // 3. Push the entire trace to the logger
      VTB_LOG(ERROR) << ss.str();
   }

   // 4. THE MOST IMPORTANT PART
   // Force the logger to write the buffer to the file right now.
   // Without this, the logs you just created will stay in the buffer and die with the process.
   vtb::Logger::get_instance().emergency_flush();

   // 5. Termination
   if (sig == SIGINT || sig == SIGTERM) {
      _exit(0); // Clean exit for user interrupts
   } else {
      // Restore default and raise to allow system core dump for debugging
      std::signal(sig, SIG_DFL);
      std::raise(sig);
   }
}

*/

// static void signal_handler(int sig) {
//    // 1. Disable handlers immediately to prevent recursive crashes
//    std::signal(SIGSEGV, SIG_DFL);
//    std::signal(SIGABRT, SIG_DFL);
// 
//    // 2. Format the crash info manually
//    std::stringstream ss;
//    ss << "\n" << std::string(50, '=') << "\n";
//    ss << "CRASH SIGNAL: " << sig << "\n";
// 
//    // 3. Capture Stack Trace
//    void* array[20];
//    size_t size = backtrace(array, 20);
//    char** symbols = backtrace_symbols(array, size);
// 
//    if (symbols) {
//       ss << "Stack Trace:\n";
//       for (size_t i = 0; i < size; i++) {
//          // Demangle logic...
//          int status;
//          char* demangled = abi::__cxa_demangle(symbols[i], nullptr, nullptr, &status);
//          ss << "  [" << i << "] " << (status == 0 ? demangled : symbols[i]) << "\n";
//          free(demangled);
//       }
//       free(symbols);
//    }
//    ss << std::string(50, '=') << "\n";
// 
//    // 4. Bypassing the normal VTB_LOG macro and pushing directly to buffer
//    // This avoids the background thread dependency
//    {
//       auto& logger = vtb::Logger::get_instance();
//       // Directly append to the buffer instead of waiting for a macro
//       logger.direct_append(ss.str()); 
//       logger.emergency_flush();
//    }
// 
//    // 5. Re-raise so we get the core dump for GDB
//    std::raise(sig);
//    _exit(0);
// }

