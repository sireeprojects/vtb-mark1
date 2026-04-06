#pragma once

#include <any>
#include <map>
#include <mutex>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <stdexcept>

#include "cmdline_parser.h"
#include "common.h"
#include "messenger.h"

namespace vtb {

class ConfigManager {
public:
   static ConfigManager& get_instance();
   ConfigManager(const ConfigManager&) = delete;
   ConfigManager& operator=(const ConfigManager&) = delete;

   bool init(int argc, char** argv);
   void print_portmap();
   bool is_queue_ready(int vid, int qpid);
   void clear_device(int vid);
   void init_vhost_device(int port_id, int vid, int nof_pairs);
   void set_queue_state(int port_id, uint16_t vring_id, bool enable);
   void assign_port_data_socket(int port_id, int qp_idx, int socket_fd);
   void assign_port_control_socket(int port_id, int ctl_fd);
   std::tuple<int, uint16_t, uint16_t> get_vhost_qids(int vid, int q_num);

private:
   ConfigManager();
   ~ConfigManager();

   CmdlineParser parser_;
   std::map<int, PortMap> pmap_;
   mutable std::mutex pmap_mutex_;

   // dual map to search each other, saves search logic
   std::map<int, int> pvmap_;
   std::map<int, int> vpmap_;

   mutable std::mutex pvmap_mutex_; // CHECK if this is required

public:
   template <typename T>
   T get_arg(std::string_view name) const {
      return parser_.get<T>(name);
   }
};

}  // namespace vtb
