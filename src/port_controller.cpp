#include "port_controller.h"

namespace vtb {

void PortController::shutdown() {
   // call shutdown for ann porthandlers
   for (auto& [port_id, inner_map] : port_handler_) {
        VTB_LOG(DEBUG) << "PortController: Shutting down handlers for Port ID: " << port_id;

        // Iterate through the inner map (vid -> unique_ptr)
        for (auto& [vid, handler_ptr] : inner_map) {
            // Safety check: ensure the unique_ptr is not null before calling shutdown
            if (handler_ptr) {
                VTB_LOG(DEBUG) << "Calling shutdown for VID: " << vid;
                handler_ptr->shutdown();
            }
        }
    }
}

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

PortHandler& PortController::get_port_handler_by_vid(int target_vid) {
   // Iterate through the outer map (port_id)
   for (auto const& [port_id, inner_map] : port_handler_) {
      
      // Look for the vid in the inner map
      auto it = inner_map.find(target_vid);
      
      // If found, return the object behind the unique_ptr
      if (it != inner_map.end()) {
         // Check if the pointer is valid before dereferencing
         if (it->second) {
            return *(it->second);
         }
      }
   }

   // Throw an exception if nothing is found
   throw std::runtime_error("PortHandler with vid not found");
}

PortHandler& PortController::get_port_handler_by_port_id(int port_id) {
   // 1. Jump directly to the port_id in the outer map - O(log N)
   auto outer_it = port_handler_.find(port_id);

   // 2. Check if the port_id exists
   if (outer_it == port_handler_.end()) {
      throw std::runtime_error("Port ID not found");
   }

   // 3. Get a reference to the inner map (vid -> unique_ptr)
   auto& inner_map = outer_it->second;

   // 4. Check if the inner map is empty
   if (inner_map.empty()) {
      throw std::runtime_error("No VIDs configured for this Port ID");
   }

   // 5. Return the first available PortHandler (vid can be anything)
   // inner_map.begin() gives us the first pair in the sorted map
   auto& first_pair = *(inner_map.begin());

   if (first_pair.second) {
      return *(first_pair.second);
   }

   throw std::runtime_error("Invalid PortHandler pointer encountered");
}

}  // namespace vtb
