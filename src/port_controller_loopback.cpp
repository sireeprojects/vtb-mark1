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


// void PortControllerLoopback::shutdown_port_handler(int vid) {
//     auto it = port_handler_.find(vid);
// 
//     // Check if this VID actually exists in our map
//     if (it != port_handler_.end()) {
//         // it->second is the inner map: std::map<int, std::unique_ptr<...>>
//         for (auto const& [qid, handler_ptr] : it->second) {
//             if (handler_ptr) {
//                 handler_ptr->stop();
//             }
//         }
//     }
// }

void PortControllerLoopback::process_notification(PortDeviceRingState pdrs) {
   VTB_LOG(DEBUG) << "PortControllerLoopback: Received:"
               << "  meta: " << static_cast<int>(pdrs.meta)
               << "  device_id: " << pdrs.device_id
               << "  qid: " << pdrs.qid
               << "  enable: " << pdrs.enable;

   // device destroyed
   if (pdrs.meta == vtb::VhostNotifyMetadata::PORT_DOWN) {
      // shutdown_port_handler(pdrs.device_id);
      // auto it = port_handler_.find(pdrs.device_id);
      return;
   }

   auto it = pmap_.find(pdrs.device_id);

   if (it != pmap_.end()) { // device exists
      pmap_[pdrs.device_id][pdrs.qid] = pdrs.enable;

      if (vtb::is_even(pdrs.qid)) {
         ready_ = ((pmap_[pdrs.device_id][pdrs.qid]==1) &&
                   (pmap_[pdrs.device_id][pdrs.qid+1]==1));
         if (ready_) {
            VTB_LOG(DEBUG) << "PortControllerLoopback: Even Queues ready: " << pdrs.qid << ":"<< pdrs.qid+1;
            VTB_LOG(INFO) << "PortControllerLoopback: Even Handler called";
         }
      } else {
         ready_ = ((pmap_[pdrs.device_id][pdrs.qid]==1) && 
                   (pmap_[pdrs.device_id][pdrs.qid-1]==1));
         if (ready_) {
            VTB_LOG(DEBUG) << "PortControllerLoopback: Queue pair ready:"
               << " VID: " << pdrs.device_id
               << " RXQID: " << pdrs.qid-1
               << " TXQID: " << pdrs.qid;
            VTB_LOG(DEBUG) << "PortControllerLoopback: Queue handler called for:" 
               << " VID: " << pdrs.device_id
               << " PairID" << pdrs.qid/2;

            // Create port handler on demand and start it 
            // // // port_handler_[pdrs.device_id][pdrs.qid/2] = std::make_unique<vtb::PortHandlerLoopback>();
            // // // port_handler_[pdrs.device_id][pdrs.qid/2]->set_ids(pdrs.device_id, pdrs.qid-1, pdrs.qid);
            try {
               // port_handler_[pdrs.device_id][pdrs.qid/2]->start();
            } catch (const std::exception& e) {
               VTB_LOG(ERROR) << "PortHandler start failed: " << e.what();
            }
         }
      }
   } else {
      // CHECK what is this logic ?? try to remember
      // only for the first queuepair
      pmap_[pdrs.device_id].resize(8*2, -1); // init each element to -1
      pmap_[pdrs.device_id][pdrs.qid] = pdrs.enable;
   }
}

}  // namespace vtb
