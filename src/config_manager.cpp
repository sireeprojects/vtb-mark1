#include "config_manager.h"

#include <iomanip>
#include <sstream>

namespace vtb {

ConfigManager::ConfigManager() {
   parser_.add_argument("--help",
                        "-h", 
                        "Show this help menu", 
                        false, 
                        "false");
   parser_.add_argument("--mode", 
                        "-m", 
                        "Loopback | Back2Back | Emulator", 
                        false, 
                        "Loopback");
   parser_.add_argument("--txrx-threads",
                        "-txrxth",
                        "1 | 2", 
                        false, 
                        "1");
   parser_.add_argument("--threading-mode",
                        "-thmode",
                        "EachQ-TwoThread | EachQ-OneThread | AllQ-TwoThread | AllQ-OneThread",
                        false,
                        "EachQ-OneThread");
   parser_.add_argument("--abstract-sockname",
                        "-absn", 
                        "Specify a random name. For internal use only", 
                        false, 
                        "cm_to_ph_sock");
   parser_.add_argument("--port-data-sockname",
                        "-pdsn", 
                        "Unix socket path to connect to port data plane", 
                        false, 
                        "/tmp/port_data.sock");
   parser_.add_argument("--port-control-sockname",
                        "-pcsn", 
                        "Unix socket path to connect to port control plane", 
                        false, 
                        "/tmp/port_ctrl.sock");
   parser_.add_argument("--vhost-sockname",
                        "-vsn", 
                        "Unix socket path to connect to virtual machine", 
                        false, 
                        "/tmp/vhost.sock");
   parser_.add_argument("--verbosity", 
                        "-v", 
                        "Fatal | Error | Warning | Info | Debug | Trace", 
                        false, 
                        "Info");
}

ConfigManager& ConfigManager::get_instance() {
   static ConfigManager instance;
   return instance;
}

ConfigManager::~ConfigManager() {
}

bool ConfigManager::init(int argc, char** argv) {
   try {
      parser_.parse(argc, argv);
      if (parser_.get<bool>("--help")) {
         parser_.print_usage();
         return false;
      }
      return true;
   } catch (const std::exception& e) {
      VTB_LOG(ERROR) << "Init Error: " << e.what();
      parser_.print_usage();
      return false;
   }
}

void ConfigManager::init_vhost_device(int port_id, int vid, int nof_pairs) {
   std::lock_guard<std::mutex> lock(pmap_mutex_);

   // create entry if it does not exist
   pvmap_[port_id] = vid; 
   vpmap_[vid] = port_id; 
   PortMap& pm = pmap_[vid];

   pm.vd.nof_queue_pairs = nof_pairs;

   for (int i = 0; i < nof_pairs; i++) {

      pm.vd.qpid[i] = i;

      // Typically in vhost: RX is even (0, 2..), TX is odd (1, 3..)
      pm.vd.qp[i].rxq_id = i * 2;
      pm.vd.qp[i].txq_id = (i * 2) + 1;
      pm.vd.qp[i].rxq_enabled = false;
      pm.vd.qp[i].txq_enabled = false;

      // Initialize port fds to -1 (not connected yet)
      pm.pd.qp[i].rxq_id = -1;
      pm.pd.qp[i].txq_id = -1;

      // pm.vd.ready = true; // TODO

      pm.vd.ctlq_id = nof_pairs * 2;
   }
}

void ConfigManager::set_queue_state(int vid, uint16_t queue_id,
                                    bool enable) {
   std::lock_guard<std::mutex> lock(pmap_mutex_);

   auto it = pmap_.find(vid);

   if (it == pmap_.end()) {
      VTB_LOG(WARNING) << "ConfigManager: VID=" << vid << " not found";
      return;
   }

   VhostDevice& vd = it->second.vd;

   for (int i = 0; i < vd.nof_queue_pairs; i++) {
      if (vd.qp[i].rxq_id == queue_id) {
         vd.qp[i].rxq_enabled = enable;
         return;
      }
      if (vd.qp[i].txq_id == queue_id) {
         vd.qp[i].txq_enabled = enable;
         return;
      }
   }
}

bool ConfigManager::is_port_ready(int vid) {
    // 1. Locate the PortMap for the given vid
    auto it = pmap_.find(vid);
    
    // If the vid doesn't exist, it can't be ready
    if (it == pmap_.end()) {
        return false;
    }

    const VhostDevice& vd = it->second.vd;

    // 2. Verification check: Must have at least one queue pair to be "ready"
    // If your logic allows 0 queues to be 'ready', remove this check.
    if (vd.nof_queue_pairs <= 0) {
        return false;
    }

    // 3. Iterate through all active queue pair IDs
    for (int i = 0; i < vd.nof_queue_pairs; ++i) {
        int queue_idx = vd.qpid[i];

        // Bounds check for the qp array
        if (queue_idx < 0 || queue_idx >= MAX_QUEUE_PAIRS) {
            return false; 
        }

        // 4. Strict check: Both RX and TX must be enabled for this specific pair
        bool rx_ready = vd.qp[queue_idx].rxq_enabled;
        bool tx_ready = vd.qp[queue_idx].txq_enabled;

        if (!rx_ready || !tx_ready) {
            // If any single RX or TX is false, the "all" condition fails immediately
            return false;
        }
    }

    // If we finished the loop without returning false, 
    // it means every enabled queue pair is fully operational.
    return true;
}

void ConfigManager::assign_port_data_socket(int port_id, int qp_idx,
                                            int socket_fd) {
   std::lock_guard<std::mutex> lock(pmap_mutex_);

   if (pmap_.count(port_id) && qp_idx < vtb::MAX_QUEUE_PAIRS) {
      // We assign the same FD to both handles
      pmap_[port_id].pd.qp[qp_idx].rxq_id = socket_fd;
      pmap_[port_id].pd.qp[qp_idx].txq_id = socket_fd;
   }
   // TODO if the if() fails then print error msg
}

void ConfigManager::assign_port_control_socket(int port_id, int ctl_fd) {
   std::lock_guard<std::mutex> lock(pmap_mutex_);

   if (pmap_.count(port_id)) {
      pmap_[port_id].pd.ctlq_id = ctl_fd;
   }
}

void ConfigManager::print_portmap() {
   std::lock_guard<std::mutex> lock(pmap_mutex_);

   // Table Header
   VTB_LOG(INFO) << std::left 
                       << std::setw(8) << "Port#" 
                       << std::setw(6) << "Vid"
                       << std::setw(8) << "No Qs" 
                       << std::setw(8) << "QpID" 
                       << std::setw(8) << "RxqId" 
                       << std::setw(8) << "TxqId" 
                       << std::setw(10) << "RxqId_En" 
                       << std::setw(10) << "TxqId_En" 
                       << std::setw(8) << "Ready" 
                       << std::setw(8) << "CtrlId"
                       << std::setw(8) << "RxqFd" 
                       << std::setw(8) << "TxqFd" 
                       << "CtrlFd";

    VTB_LOG(INFO)<< std::string(110, '-');

   for (const auto& [vid, map] : pmap_) {
      const auto& vd = map.vd;
      const auto& pd = map.pd;

      for (int i = 0; i < vd.nof_queue_pairs; ++i) {
         const auto& vqp = vd.qp[i];
         const auto& pqp = pd.qp[i];
         const auto& qpid = vd.qpid[i];

         std::stringstream ss;

         if (i == 0) {
            // First row: Print Port, Vid, No Qs, Ready, CtrlID, and CtrlFd
            ss << std::left << 
               std::setw(8) << vpmap_[vid]
               << std::setw(6) << vid
               << std::setw(8) << vd.nof_queue_pairs 
               << std::setw(8) << qpid 
               << std::setw(8) << vqp.rxq_id 
               << std::setw(8) << vqp.txq_id 
               << std::setw(10) << (vqp.rxq_enabled ? "YES" : "NO") 
               << std::setw(10) << (vqp.txq_enabled ? "YES" : "NO") 
               << std::setw(8) << (vd.ready ? "YES" : "NO") 
               << std::setw(8) << vd.ctlq_id
               << std::setw(8) << (pqp.rxq_id == -1 ? "-" : std::to_string(pqp.rxq_id))
               << std::setw(8) << (pqp.txq_id == -1 ? "-" : std::to_string(pqp.txq_id))
               << pd.ctlq_id;
         } else {
            // Subsequent rows: Leave Port/Vid/NoQs/Ready/CtrlID/CtrlFd empty
            ss << std::left << std::setw(22) << " "  // Port#, Vid, No Qs
               << std::setw(8) << qpid 
               << std::setw(8) << vqp.rxq_id << std::setw(8) << vqp.txq_id
               << std::setw(10) << (vqp.rxq_enabled ? "YES" : "NO")
               << std::setw(10) << (vqp.txq_enabled ? "YES" : "NO")
               << std::setw(8) << " "  // Ready
               << std::setw(8) << " "  // CtrlID
               << std::setw(8)
               << (pqp.rxq_id == -1 ? "-" : std::to_string(pqp.rxq_id))
               << std::setw(8)
               << (pqp.txq_id == -1 ? "-" : std::to_string(pqp.txq_id))
               << " ";  // CtrlFd
         }

         VTB_LOG(INFO) << ss.str();
      }
      VTB_LOG(INFO) << std::string(110, '-');
   }
}

std::tuple<int, uint16_t, uint16_t> ConfigManager::get_vhost_qids(int vid,
                                                                  int q_num) {
   std::lock_guard<std::mutex> lock(pmap_mutex_);

   auto it = pmap_.find(vid);
   if (it == pmap_.end()) {
      // Return -1 for vid to indicate the port was not found
      return {-1, 0, 0};
   }

   const VhostDevice& vd = it->second.vd;

   // Ensure the requested queue number is within the valid range for this
   // device
   if (q_num < 0 || q_num >= vd.nof_queue_pairs) {
      return {-1, 0, 0};
   }

   // Return the rxq_id and txq_id of the specific queue pair index (q_num)
   // for the Vhost side of the particular port.
   uint16_t rxq = vd.qp[q_num].rxq_id;
   uint16_t txq = vd.qp[q_num].txq_id;

   return {vid, rxq, txq};
}

bool ConfigManager::is_queue_ready(int vid, int qpid) {
   std::lock_guard<std::mutex> lock(pmap_mutex_);

   auto it = pmap_.find(vid);
   if (it == pmap_.end()) {
      return false;
   }
   const VhostDevice& vd = it->second.vd;

   if (vd.qp[qpid].rxq_enabled && vd.qp[qpid].txq_enabled) {
      return true;
   }
   return false;
}

void ConfigManager::clear_device(int vid) {
   std::lock_guard<std::mutex> lock(pmap_mutex_);

   // do not change the order of delete
   size_t  pmap_erased_count = pmap_.erase(vid);
   size_t pvmap_erased_count = pvmap_.erase(vpmap_[vid]);

   // delete vpmap_ last because it is used in the deletion of pvmap
   size_t vpmap_erased_count = vpmap_.erase(vid);

   if (pmap_erased_count == 0) {
      VTB_LOG(FATAL) << "ConfigManager: Device Id: " << vid << " was not found in PortMap";
   } else {
      VTB_LOG(DEBUG) << "ConfigManager: Device Id: " << vid << " is remove from PortMap";
   }
   if (pvmap_erased_count == 0) {
      VTB_LOG(FATAL) << "ConfigManager: Device Id: " << vid << " was not found in PVMAP";
   } else {
      VTB_LOG(DEBUG) << "ConfigManager: Device Id: " << vid << " is remove from PVMAP";
   }
   if (vpmap_erased_count == 0) {
      VTB_LOG(FATAL) << "ConfigManager: Device Id: " << vid << " was not found in VPMAP";
   } else {
      VTB_LOG(DEBUG) << "ConfigManager: Device Id: " << vid << " is remove from VPMAP";
   }
}

}  // namespace vtb
