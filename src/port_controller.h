#pragma once

#include <atomic>
#include <thread>
#include <map>
#include <memory>

#include "config_manager.h"
#include "messenger.h"
#include "port_handler.h"
#include "port_handler_loopback.h"

namespace vtb {

class PortController {
public:
   PortController() = default;
   virtual ~PortController() = default;

   PortController(const PortController&) = delete;
   PortController& operator=(const PortController&) = delete;

   void start();
   void shutdown();

protected:
   virtual void create_server() = 0;
   virtual void monitor_and_dispatch_handler() = 0;
   virtual void epoll_worker() = 0;
   void launch_worker();

   PortHandler& get_port_handler_by_vid(int target_vid);
   PortHandler& get_port_handler_by_port_id(int port_id);

   std::thread worker_;
   std::atomic<bool> is_running_{false};
   std::map<int, std::map<int, std::unique_ptr<vtb::PortHandler>>> port_handler_;
};

}  // namespace vtb
