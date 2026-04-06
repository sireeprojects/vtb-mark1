#include "port_controller.h"

namespace vtb {

void PortController::start() {
   create_server();
   monitor_and_dispatch_handler();
}

void PortController::launch_worker() {
   if (!is_running_) {
      is_running_ = true;
      worker_ = std::thread(&PortController::epoll_worker, this);
      vtb::set_thread_name(worker_, "epollWorker");
      worker_.detach();
   }
}

}  // namespace vtb
