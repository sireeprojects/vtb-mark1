#include "port_controller_loopback.h"

namespace vtb {

PortControllerLoopback::~PortControllerLoopback() {
   VTB_LOG(INFO) << "PortControllerLoopback: Cleaning up...";
   is_running_ = false;
   if (worker_.joinable()) {
      worker_.join();
   }
   close(abstract_fd_); // TODO check fd before closing
}

void PortControllerLoopback::create_server() {
   abstract_sockname_ = vtb::ConfigManager::get_instance().get_arg<std::string>("--abstract-sockname");
   std::string sock_path = std::string(1, '\0') + abstract_sockname_;

   if (sock_path.size() > 0 && sock_path[0] == '\0') {
      VTB_LOG(DEBUG) << "PortControllerLoopback: First byte is NULL in abstract socket";
   }
   VTB_LOG(INFO) << "PortControllerLoopback: Started Loopback Controller with " << abstract_sockname_;
   abstract_fd_ = vtb::create_server_socket(sock_path);
}

void PortControllerLoopback::monitor_and_dispatch_handler() {
   listen(abstract_fd_, 5);
   epoll_fd = epoll_create1(0);
   ev.events = EPOLLIN;
   ev.data.fd = abstract_fd_;
   epoll_ctl(epoll_fd, EPOLL_CTL_ADD, abstract_fd_, &ev);
   VTB_LOG(INFO) << "PortControllerLoopback: Launching epoll worker";
   launch_worker();
}

void PortControllerLoopback::epoll_worker() {
   PortDeviceRingState pdrs_{};
   while (is_running_) {
      int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
      for (int n = 0; n < nfds; ++n) {
         int fd = events[n].data.fd;

         if (fd == abstract_fd_) {
            int client_fd = accept(abstract_fd_, NULL, NULL);
            VTB_LOG(INFO)
                << "PortControllerLoopback: Connected to VhostController with Fd: "
                << client_fd;
            struct epoll_event client_ev;
            client_ev.events = EPOLLIN | EPOLLRDHUP;
            client_ev.data.fd = client_fd;
            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev);
         } else if (events[n].events & EPOLLIN) {
            ssize_t bytes = read(fd, &pdrs_, sizeof(pdrs_));

            if (bytes <= 0) {
               VTB_LOG(INFO) << "PortControllerLoopback: Disconnected to VhostController with Fd: "
                             << fd;
               epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
               close(fd);
            } else if (bytes == sizeof(PortDeviceRingState)) {
               process_notification(pdrs_);
            }
         }
      }
   }
}

void PortControllerLoopback::add_handler(int pid, int vid) {
   VTB_LOG(DEBUG) << "PortControllerLoopback: add_handler:"
                  << " pid: " << pid
                  << " vid: " << vid;
   // 1. Create the unique_ptr (assuming a constructor like PortHandler(vid))
   auto handler = std::make_unique<vtb::PortHandlerLoopback>();

   // 2. Move it into the nested map
   // port_handler_[pid] finds or creates the inner map
   // [vid] finds or creates the unique_ptr slot
   port_handler_[pid][vid] = std::move(handler);
}

void PortControllerLoopback::remove_handler(int pid, int vid) {
   // 1. Find the inner map for the pid
   auto it_pid = port_handler_.find(pid);

   if (it_pid != port_handler_.end()) {
      // 2. Erase the specific vid from the inner map
      // This destroys the unique_ptr and calls the PortHandler destructor
      size_t erased = it_pid->second.erase(vid);

      if (erased > 0) {
         VTB_LOG(DEBUG) << "Successfully removed handler for PID: " << pid << " VID: " << vid;
      }

      // 3. Optional: If the inner map is now empty, remove the pid entry entirely
      if (it_pid->second.empty()) {
         port_handler_.erase(it_pid);
      }
   }
   VTB_LOG(DEBUG) << "PortControllerLoopback: remove_handler: pid not found: " << pid;
}

void PortControllerLoopback::process_notification(PortDeviceRingState pdrs) {
   VTB_LOG(DEBUG) << "PortControllerLoopback: Received:"
               << "  meta: " << static_cast<int>(pdrs.meta)
               << "  port_id: " << pdrs.pid
               << "  device_id: " << pdrs.vid;

   // VM shutdown/crashed
   if (pdrs.meta == vtb::VhostNotifyMetadata::PORT_DOWN) {
      try {
         VTB_LOG(DEBUG) << "PortControllerLoopback: Handler Size: " << port_handler_.size();
         auto& handler = get_port_handler_by_vid(pdrs.vid);
         VTB_LOG(DEBUG) << "PortControllerLoopback: Handler Size: " << port_handler_.size();
         handler.shutdown();
         VTB_LOG(DEBUG) << "PortControllerLoopback: Handler Size: " << port_handler_.size();
         remove_handler(pdrs.pid, pdrs.vid);
         VTB_LOG(DEBUG) << "PortControllerLoopback: Handler Size: " << port_handler_.size();
      } catch (const std::runtime_error& e) { // device_id wasn't found
         VTB_LOG(ERROR) << "PortControllerLoopback: DOWN, VID: " << pdrs.vid << " not found";
      }
      return;
   }

   // dispatch port handler threads
   if (pdrs.meta == vtb::VhostNotifyMetadata::PORT_UP) {
      try {
         add_handler(pdrs.pid, pdrs.vid);
         auto& handler = get_port_handler_by_vid(pdrs.pid);
         handler.start(pdrs.pid, pdrs.vid);
      } catch (const std::runtime_error& e) { // device_id wasn't found
         VTB_LOG(ERROR) << "PortControllerLoopback: UP, VID: " << pdrs.vid << " not found";
      }
      return;
   }
}

}  // namespace vtb
