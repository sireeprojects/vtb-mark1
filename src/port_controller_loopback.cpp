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

void PortControllerLoopback::process_notification(PortDeviceRingState pdrs) {
   VTB_LOG(DEBUG) << "PortControllerLoopback: Received:"
               << "  meta: " << static_cast<int>(pdrs.meta)
               << "  port_id: " << pdrs.pid
               << "  device_id: " << pdrs.vid;

   // VM shutdown/crashed
   if (pdrs.meta == vtb::VhostNotifyMetadata::PORT_DOWN) {
      try {
         auto& handler = get_port_handler_by_vid(pdrs.vid);
         handler.shutdown();
      } catch (const std::runtime_error& e) { // device_id wasn't found
         std::cerr << "Error: " << e.what() << std::endl;
      }
      return;
   }

   // dispatch port handler threads
   if (pdrs.meta == vtb::VhostNotifyMetadata::PORT_UP) {
      try {
         auto& handler = get_port_handler_by_vid(pdrs.pid);
         handler.start();
      } catch (const std::runtime_error& e) { // device_id wasn't found
         std::cerr << "Error: " << e.what() << std::endl;
      }
      return;
   }
}

}  // namespace vtb
