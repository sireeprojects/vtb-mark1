#pragma once

#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <string>
#include <algorithm>

#include "port_controller.h"

namespace vtb {

class PortControllerLoopback : public PortController {
public:
   PortControllerLoopback() = default;
   virtual ~PortControllerLoopback();

protected:
   void create_server() override;
   void monitor_and_dispatch_handler() override;
   void epoll_worker() override;

private:
   static constexpr int MAX_EVENTS = 10;
   static constexpr int BUFFER_SIZE = 1024;

   void process_notification(PortDeviceRingState pdrs);

   void add_handler(int pid, int vid);
   void remove_handler(int pid, int vid);

   std::string abstract_sockname_{};
   int abstract_fd_{-1};
   int epoll_fd{-1};
   struct epoll_event ev, events[MAX_EVENTS];
   bool ready_{false};
};

}  // namespace vtb
