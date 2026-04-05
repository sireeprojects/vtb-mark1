#pragma once

#include <any>
#include <map>
#include <mutex>
#include <optional>
#include <tuple>
#include <unordered_map>

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

   template <typename T>
   T get_arg(std::string_view name) const {
      return parser_.get<T>(name);
   }

   template <typename T>
   void set_value(const std::string& key, T&& value) {
      std::lock_guard<std::mutex> lock(db_mutex_);
      database_[key] =
          std::make_any<typename std::decay<T>::type>(std::forward<T>(value));
   }

   template <typename T>
   std::optional<T> get_value(const std::string& key) const {
      std::lock_guard<std::mutex> lock(db_mutex_);
      auto it = database_.find(key);
      if (it != database_.end()) {
         try {
            return std::any_cast<T>(it->second);
         } catch (...) {
         }
      }
      return std::nullopt;
   }

   void dump_config();
   void print_portmap();

   void init_vhost_device(int port_id, int vid, int nof_pairs);
   void set_queue_state(int port_id, uint16_t vring_id, bool enable);
   void assign_port_data_socket(int port_id, int qp_idx, int socket_fd);
   void assign_port_control_socket(int port_id, int ctl_fd);
   std::tuple<int, uint16_t, uint16_t> get_vhost_qids(int port_id, int q_num);

   bool is_queue_ready(int vid, int qpid);
   void clear_device(int vid);

private:
   ConfigManager();
   ~ConfigManager();

   CmdlineParser parser_;
   std::unordered_map<std::string, std::any> database_;
   mutable std::mutex db_mutex_;

   std::map<int, PortMap> pmap_;
   mutable std::mutex pmap_mutex_;
};

}  // namespace vtb
