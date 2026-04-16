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
   void set_queue_state(int vid, uint16_t queue_id, bool enable);
   bool is_port_ready(int vid);

   // used from port side
   void assign_port_data_socket(int port_id, int qp_idx, int socket_fd);
   void assign_port_control_socket(int port_id, int ctl_fd);

   const std::map<int, PortMap>& get_pmap() const;

   int get_pid_by_vid(int vid);
   int get_vid_by_pid(int pid);

   // Accessor for the stats table
   QueueStats (&get_stats_table())[MAX_VHOST_DEVICES][MAX_QUEUES] {
      return stats_table_;
   }

   void print_final_report();   
   void clear_statistics();

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

   QueueStats stats_table_[MAX_VHOST_DEVICES][MAX_QUEUES];

public:
   template <typename T>
   T get_arg(std::string_view name) const {
      return parser_.get<T>(name);
   }
};

}  // namespace vtb
