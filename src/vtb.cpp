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

bool keep_running{false};
static int keep_alive_thread(void*);
static void signal_handler(int);

int main(int argc, char** argv) {
   vtb::disable_echoctl();

   // assign signal handlers
   std::set_terminate(vtb::graceful_exit);
   std::signal(SIGINT, signal_handler);
   std::signal(SIGTERM, signal_handler);

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
   // auto mode = config.get_arg<std::string>("--mode");
   // std::unique_ptr<vtb::PortController> port_controller = vtb::create_controller(mode);
   // port_controller->start();

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

static void signal_handler(int) {
   VTB_LOG(FATAL) << "<User Pressed Ctrl+C>";
   keep_running = true;
}

static int keep_alive_thread(void*) {
   while (!keep_running) {
      rte_pause();
   }
   return 0;
}
