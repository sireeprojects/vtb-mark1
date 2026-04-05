#include <csignal>

#include "cmdline_parser.h"
#include "common.h"
#include "config_manager.h"
#include "logger.h"
#include "messenger.h"
// #include "port_controller_loopback.h"
#include "vhost_controller.h"

bool stop_blocking_{false};

static int keep_alive(void*) {
   while (!stop_blocking_) {
      rte_pause();
   }
   return 0;
}

static void signal_handler(int) {
   vtb::info() << "<User Pressed Ctrl+C>";
   stop_blocking_ = true;
}

// CHECK
void my_graceful_exit() {
    // Note: We don't have access to the 'runtime_error' object here easily
    vtb::error() << "A fatal error occurred. Shutting down gracefully...";
    std::exit(1);
}

int main(int argc, char** argv) {
   vtb::disable_echoctl();

   // assign signal handler for graceful exit
   std::signal(SIGINT, signal_handler);
   std::signal(SIGTERM, signal_handler);

   // setup logger
   // vtb::Logger::get_instance().init("run.log", vtb::LogLevel::DEFAULT);
   vtb::Logger::get_instance().init("run.log", vtb::LogLevel::FULL);

   std::set_terminate(my_graceful_exit);

   // setup configuration manager
   auto& config = vtb::ConfigManager::get_instance();
   if (!config.init(argc, argv)) {
      vtb::error() << "Config.Init failed";
      return -1;
   }

   // start the appropriate port controller
   auto mode = config.get_arg<std::string>("-m");
   // std::unique_ptr<vtb::PortController> port_controller = vtb::create_controller(mode);

   // port_controller->start();

   // start vhost controller
   auto socket_path = config.get_arg<std::string>("-vsn");
   vtb::VhostController backend(socket_path.c_str());

   backend.init(argc, argv);
   backend.start();

   rte_eal_remote_launch(keep_alive, NULL, 2);
   rte_eal_mp_wait_lcore();

   // config.print_portmap();
   // port_controller.reset();

   vtb::restore_echoctl();

   // rte_eal_cleanup(); // create drouble free issue when trying to cleanup 
                         // in class desctructors 

   vtb::info() << "Test Done. Starting cleanup...";
   return 0;
}

// vhost and eal messages to just errors
// rte_log_set_level_pattern("lib.vhost.config", RTE_LOG_ERR);
// rte_log_set_level_pattern("lib.eal", RTE_LOG_ERR);

// config.dump_config();
// check vhost devices and queues for ports
// config.print_portmap();

// auto absn = config.get_arg<std::string>("-absn");
// vtb::info() << "Abstract Socket Name: " << absn;

// auto pdsn = config.get_arg<std::string>("-pdsn");
// vtb::info() << "Port Data Socket Name: " << pdsn;

// auto pcsn = config.get_arg<std::string>("-pcsn");
// vtb::info() << "Port Control Socket Name: " << pcsn;

// auto vsn  = config.get_arg<std::string>("-vsn");
// vtb::info() << "Vhost Socket Name: " << vsn;
