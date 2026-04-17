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
   parser_.add_argument("--client", 
                        "-c", 
                        "Operate as Vhost Client", 
                        false, 
                        "false");
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

// Returns the VID associated with a PID, or -1 if not found
int ConfigManager::get_vid_by_pid(int pid) {
   auto it = pvmap_.find(pid);
   if (it != pvmap_.end()) {
      return it->second;
   }
   return -1; 
}

// Returns the PID associated with a VID, or -1 if not found
int ConfigManager::get_pid_by_vid(int vid) {
   auto it = vpmap_.find(vid);
   if (it != vpmap_.end()) {
      return it->second;
   }
   return -1;
}

void ConfigManager::init_vhost_device(int port_id, int vid, int nof_pairs) {
   std::lock_guard<std::mutex> lock(pmap_mutex_);

   pmap_.emplace(vid, PortMap{});

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
      // CHECK and revert
      // VTB_LOG(WARNING) << "ConfigManager: VID=" << vid << " not found";
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

const std::map<int, PortMap>& ConfigManager::get_pmap() const {
   return pmap_;
}

void ConfigManager::clear_statistics() {
    for (int v = 0; v < MAX_VHOST_DEVICES; ++v) {
        for (int q = 0; q < MAX_QUEUES; ++q) {
            auto& s = stats_table_[v][q];
            s.tx_frames.store(0, std::memory_order_relaxed);
            s.rx_frames.store(0, std::memory_order_relaxed);
            s.tx_dropped.store(0, std::memory_order_relaxed);
            s.rx_dropped.store(0, std::memory_order_relaxed);
        }
    }
    VTB_LOG(INFO) << "All traffic statistics have been cleared.";
}

// with manual prefix
void ConfigManager::print_final_report() {
    const int total_width = 80;
    const std::string title = "VTB CONSOLIDATED PERFORMANCE REPORT";
    const std::string prefix = "[...INFO] "; // Manually prepending the logger prefix
    int padding = (total_width - (int)title.length()) / 2;

    std::stringstream report;
    uint64_t total_tx = 0, total_rx = 0, total_txd = 0, total_rxd = 0;

    // Start with a leading newline to separate from previous logs
    report << "\n";
    
    // Top border
    report << prefix << std::string(total_width, '=') << "\n";
    report << prefix << std::string(padding, ' ') << title << "\n";
    report << prefix << std::string(total_width, '=') << "\n";

    // Header Row
    std::stringstream header_ss;
    header_ss << std::left  << std::setw(6)  << "VID"
              << std::setw(6)  << "QP"
              << std::right << std::setw(14) << "TX Frames"
              << std::setw(14) << "RX Frames"
              << std::setw(13) << "TX Drp"
              << std::setw(13) << "RX Drp";
    report << prefix << header_ss.str() << "\n";
    report << prefix << std::string(total_width, '-') << "\n";

    bool any_device_active = false;

    for (int v = 0; v < get_nof_vids(); ++v) {
        bool vid_has_data = true;
        // Check if this VID has any traffic
        // for (int q = 0; q < MAX_QUEUES; ++q) {
        //     if (stats_table_[v][q].tx_frames.load(std::memory_order_relaxed) > 0 ||
        //         stats_table_[v][q].rx_frames.load(std::memory_order_relaxed) > 0) {
        //         vid_has_data = true;
        //         break;
        //     }
        // }
        if (vid_has_data) {
            any_device_active = true;
            // Only print negotiated queues
            int nof_queues = get_nof_queues(v); 

            for (int q = 0; q < (nof_queues*2); q += 2) {
                auto& s_even = stats_table_[v][q];      // RX side
                auto& s_odd  = stats_table_[v][q + 1];  // TX side

                uint64_t rx  = s_even.rx_frames.load(std::memory_order_relaxed);
                uint64_t rxd = s_even.rx_dropped.load(std::memory_order_relaxed);
                uint64_t tx  = s_odd.tx_frames.load(std::memory_order_relaxed);
                uint64_t txd = s_odd.tx_dropped.load(std::memory_order_relaxed);

                total_tx += tx; total_rx += rx;
                total_txd += txd; total_rxd += rxd;

                std::stringstream row_ss;
                row_ss << std::left  << std::setw(6)  << v
                       << std::setw(6)  << (q / 2)
                       << std::right << std::setw(14) << tx
                       << std::setw(14) << rx
                       << std::setw(13) << txd
                       << std::setw(13) << rxd;
                report << prefix << row_ss.str() << "\n";
            }
            report << prefix << std::string(total_width, '-') << "\n";
        }
    }

    if (any_device_active) {
        std::stringstream total_ss;
        total_ss << std::left  << std::setw(12) << "TOTAL"
                 << std::right << std::setw(14) << total_tx
                 << std::setw(14) << total_rx
                 << std::setw(13) << total_txd
                 << std::setw(13) << total_rxd;
        report << prefix << total_ss.str() << "\n";
    } else {
        report << prefix << "          No traffic processed across any Vhost devices.\n";
    }

    report << prefix << std::string(total_width, '=') << std::endl;
    report << prefix;

    // Make exactly ONE call to the logger. 
    // This pushes the entire pre-formatted block into the log stream atomically.
    VTB_LOG(INFO) << report.str();
}

// with total row but with multiple vtb_log
// void ConfigManager::print_final_report() {
//     const int total_width = 80;
//     const std::string title = "VTB CONSOLIDATED PERFORMANCE REPORT";
//     int padding = (total_width - (int)title.length()) / 2;
// 
//     std::stringstream ss;
//     uint64_t total_tx = 0, total_rx = 0, total_txd = 0, total_rxd = 0;
// 
//     VTB_LOG(INFO) << std::string(total_width, '=');
//     ss.str(""); ss.clear();
//     ss << std::string(padding, ' ') << title;
//     VTB_LOG(INFO) << ss.str();
//     VTB_LOG(INFO) << std::string(total_width, '=');
// 
//     // Header
//     ss.str(""); ss.clear();
//     ss << std::left  << std::setw(6)  << "VID" 
//        << std::setw(6)  << "QP" 
//        << std::right << std::setw(14) << "TX Frames" 
//        << std::setw(14) << "RX Frames" 
//        << std::setw(13) << "TX Drp" 
//        << std::setw(13) << "RX Drp";
//     VTB_LOG(INFO) << ss.str();
//     VTB_LOG(INFO) << std::string(total_width, '-');
// 
//     bool any_device_active = false;
// 
//     for (int v = 0; v < MAX_VHOST_DEVICES; ++v) {
//         bool vid_has_data = false;
//         // Pre-check for VID activity
//         for (int q = 0; q < MAX_QUEUES; ++q) {
//             if (stats_table_[v][q].tx_frames.load(std::memory_order_relaxed) > 0 ||
//                 stats_table_[v][q].rx_frames.load(std::memory_order_relaxed) > 0) {
//                 vid_has_data = true;
//                 break;
//             }
//         }
// 
//         if (vid_has_data) {
//             any_device_active = true;
//             // TODO
//             // for (int q = 0; q < MAX_QUEUES; q += 2) {
//             for (int q = 0; q < get_nof_queues(v); q += 2) {
//                 auto& s_even = stats_table_[v][q];
//                 auto& s_odd  = stats_table_[v][q + 1];
// 
//                 uint64_t rx  = s_even.rx_frames.load(std::memory_order_relaxed);
//                 uint64_t rxd = s_even.rx_dropped.load(std::memory_order_relaxed);
//                 uint64_t tx  = s_odd.tx_frames.load(std::memory_order_relaxed);
//                 uint64_t txd = s_odd.tx_dropped.load(std::memory_order_relaxed);
// 
//                 // Add to Global Totals
//                 total_tx += tx; total_rx += rx;
//                 total_txd += txd; total_rxd += rxd;
// 
//                 ss.str(""); ss.clear();
//                 ss << std::left  << std::setw(6)  << v 
//                    << std::setw(6)  << (q / 2)
//                    << std::right << std::setw(14) << tx 
//                    << std::setw(14) << rx 
//                    << std::setw(13) << txd 
//                    << std::setw(13) << rxd;
//                 VTB_LOG(INFO) << ss.str();
//             }
//             VTB_LOG(INFO) << std::string(total_width, '-');
//         }
//     }
// 
//     if (any_device_active) {
//         // Print the Total Row
//         ss.str(""); ss.clear();
//         ss << std::left  << std::setw(12) << "TOTAL" 
//            << std::right << std::setw(14) << total_tx 
//            << std::setw(14) << total_rx 
//            << std::setw(13) << total_txd 
//            << std::setw(13) << total_rxd;
//         VTB_LOG(INFO) << ss.str();
//     } else {
//         VTB_LOG(INFO) << "          No traffic processed across any Vhost devices.";
//     }
// 
//     VTB_LOG(INFO) << std::string(total_width, '=');
// }

// good without total row
// void ConfigManager::print_final_report() {
//     const int total_width = 80; // Tightened from 85 to 80
//     const std::string title = "VTB CONSOLIDATED PERFORMANCE REPORT";
//     int padding = (total_width - (int)title.length()) / 2;
// 
//     std::stringstream ss;
// 
//     VTB_LOG(INFO) << std::string(total_width, '=');
//     ss.str(""); ss.clear();
//     ss << std::string(padding, ' ') << title;
//     VTB_LOG(INFO) << ss.str();
//     VTB_LOG(INFO) << std::string(total_width, '=');
// 
//     // Adjusted widths: VID(6), QP(4), TX(14), RX(14), TXD(12), RXD(12)
//     ss.str(""); ss.clear();
//     ss << std::left  << std::setw(6)  << "VID" 
//        << std::setw(6)  << "QP" 
//        << std::right << std::setw(14) << "TX Frames" 
//        << std::setw(14) << "RX Frames" 
//        << std::setw(13) << "TX Drp" 
//        << std::setw(13) << "RX Drp";
//     VTB_LOG(INFO) << ss.str();
//     VTB_LOG(INFO) << std::string(total_width, '-');
// 
//     bool any_device_active = false;
// 
//     for (int v = 0; v < MAX_VHOST_DEVICES; ++v) {
//         bool vid_has_data = false;
//         for (int q = 0; q < MAX_QUEUES; ++q) {
//             if (stats_table_[v][q].tx_frames.load(std::memory_order_relaxed) > 0 ||
//                 stats_table_[v][q].rx_frames.load(std::memory_order_relaxed) > 0) {
//                 vid_has_data = true;
//                 break;
//             }
//         }
// 
//         if (vid_has_data) {
//             any_device_active = true;
//             for (int q = 0; q < MAX_QUEUES; q += 2) {
//                 auto& s_even = stats_table_[v][q];
//                 auto& s_odd  = stats_table_[v][q + 1];
// 
//                 uint64_t rx  = s_even.rx_frames.load(std::memory_order_relaxed);
//                 uint64_t rxd = s_even.rx_dropped.load(std::memory_order_relaxed);
//                 uint64_t tx  = s_odd.tx_frames.load(std::memory_order_relaxed);
//                 uint64_t txd = s_odd.tx_dropped.load(std::memory_order_relaxed);
// 
//                 ss.str(""); ss.clear();
//                 int qp_index = q / 2;
// 
//                 ss << std::left  << std::setw(6)  << v 
//                    << std::setw(6)  << qp_index
//                    << std::right << std::setw(14) << tx 
//                    << std::setw(14) << rx 
//                    << std::setw(13) << txd 
//                    << std::setw(13) << rxd;
//                 VTB_LOG(INFO) << ss.str();
//             }
//             VTB_LOG(INFO) << std::string(total_width, '-');
//         }
//     }
// 
//     if (!any_device_active) {
//         VTB_LOG(INFO) << "          No traffic processed across any Vhost devices.";
//     }
// 
//     VTB_LOG(INFO) << std::string(total_width, '=');
// }

// void ConfigManager::print_final_report() {
//     const int total_width = 85;
//     const std::string title = "VTB CONSOLIDATED PERFORMANCE REPORT";
//     int padding = (total_width - (int)title.length()) / 2;
// 
//     std::stringstream ss;
// 
//     VTB_LOG(INFO) << std::string(total_width, '=');
//     ss.str(""); ss.clear();
//     ss << std::string(padding, ' ') << title;
//     VTB_LOG(INFO) << ss.str();
//     VTB_LOG(INFO) << std::string(total_width, '=');
// 
//     ss.str(""); ss.clear();
//     ss << std::left  << std::setw(6)  << "VID"
//        << std::setw(10) << "QP"           // Changed label to Queue Pair (QP)
//        << std::right << std::setw(16) << "TX Frames"
//        << std::setw(16) << "RX Frames"
//        << std::setw(15) << "TX Drp"
//        << std::setw(15) << "RX Drp";
//     VTB_LOG(INFO) << ss.str();
//     VTB_LOG(INFO) << std::string(total_width, '-');
// 
//     bool any_device_active = false;
// 
//     for (int v = 0; v < MAX_VHOST_DEVICES; ++v) {
//         bool vid_has_data = false;
//         for (int q = 0; q < MAX_QUEUES; ++q) {
//             if (stats_table_[v][q].tx_frames.load(std::memory_order_relaxed) > 0 ||
//                 stats_table_[v][q].rx_frames.load(std::memory_order_relaxed) > 0) {
//                 vid_has_data = true;
//                 break;
//             }
//         }
// 
//         if (vid_has_data) {
//             any_device_active = true;
//             for (int q = 0; q < MAX_QUEUES; q += 2) {
//                 auto& s_even = stats_table_[v][q];
//                 auto& s_odd  = stats_table_[v][q + 1];
// 
//                 uint64_t rx  = s_even.rx_frames.load(std::memory_order_relaxed);
//                 uint64_t rxd = s_even.rx_dropped.load(std::memory_order_relaxed);
//                 uint64_t tx  = s_odd.tx_frames.load(std::memory_order_relaxed);
//                 uint64_t txd = s_odd.tx_dropped.load(std::memory_order_relaxed);
// 
//                 ss.str(""); ss.clear();
// 
//                 // Calculate the pair index (0, 1, 2... up to 7)
//                 int qp_index = q / 2;
// 
//                 ss << std::left  << std::setw(6)  << v
//                    << std::setw(10) << qp_index
//                    << std::right << std::setw(16) << tx
//                    << std::setw(16) << rx
//                    << std::setw(15) << txd
//                    << std::setw(15) << rxd;
//                 VTB_LOG(INFO) << ss.str();
//             }
//             VTB_LOG(INFO) << std::string(total_width, '-');
//         }
//     }
// 
//     if (!any_device_active) {
//         VTB_LOG(INFO) << "             No traffic processed across any Vhost devices.";
//     }
// 
//     VTB_LOG(INFO) << std::string(total_width, '=');
// }

// void ConfigManager::print_final_report() {
//     const int total_width = 85;
//     const std::string title = "VTB CONSOLIDATED PERFORMANCE REPORT";
//     int padding = (total_width - (int)title.length()) / 2;
// 
//     std::stringstream ss;
// 
//     VTB_LOG(INFO) << std::string(total_width, '=');
//     ss.str(""); ss.clear();
//     ss << std::string(padding, ' ') << title;
//     VTB_LOG(INFO) << ss.str();
//     VTB_LOG(INFO) << std::string(total_width, '=');
// 
//     ss.str(""); ss.clear();
//     ss << std::left  << std::setw(6)  << "VID"
//        << std::setw(10) << "Q-PAIR"
//        << std::right << std::setw(16) << "TX Frames"
//        << std::setw(16) << "RX Frames"
//        << std::setw(15) << "TX Drp"
//        << std::setw(15) << "RX Drp";
//     VTB_LOG(INFO) << ss.str();
//     VTB_LOG(INFO) << std::string(total_width, '-');
// 
//     bool any_device_active = false;
// 
//     for (int v = 0; v < MAX_VHOST_DEVICES; ++v) {
//         // First, check if this VID has been used at all
//         bool vid_has_data = false;
//         for (int q = 0; q < MAX_QUEUES; ++q) {
//             if (stats_table_[v][q].tx_frames.load(std::memory_order_relaxed) > 0 ||
//                 stats_table_[v][q].rx_frames.load(std::memory_order_relaxed) > 0) {
//                 vid_has_data = true;
//                 break;
//             }
//         }
// 
//         // If the VID is active, print ALL its negotiated pairs
//         if (vid_has_data) {
//             any_device_active = true;
//             for (int q = 0; q < MAX_QUEUES; q += 2) {
//                 auto& s_even = stats_table_[v][q];
//                 auto& s_odd  = stats_table_[v][q + 1];
// 
//                 uint64_t rx  = s_even.rx_frames.load(std::memory_order_relaxed);
//                 uint64_t rxd = s_even.rx_dropped.load(std::memory_order_relaxed);
//                 uint64_t tx  = s_odd.tx_frames.load(std::memory_order_relaxed);
//                 uint64_t txd = s_odd.tx_dropped.load(std::memory_order_relaxed);
// 
//                 ss.str(""); ss.clear();
//                 std::string pair_label = std::to_string(q) + "/" + std::to_string(q+1);
// 
//                 ss << std::left  << std::setw(6)  << v
//                    << std::setw(10) << pair_label
//                    << std::right << std::setw(16) << tx
//                    << std::setw(16) << rx
//                    << std::setw(15) << txd
//                    << std::setw(15) << rxd;
//                 VTB_LOG(INFO) << ss.str();
//             }
//             // Add a small separator between different VIDs for clarity
//             VTB_LOG(INFO) << std::string(total_width, '-');
//         }
//     }
// 
//     if (!any_device_active) {
//         VTB_LOG(INFO) << "             No traffic processed across any Vhost devices.";
//     }
// 
//     VTB_LOG(INFO) << std::string(total_width, '=');
// }

// void ConfigManager::print_final_report() {
//     const int total_width = 85;
//     const std::string title = "VTB CONSOLIDATED PERFORMANCE REPORT";
//     int padding = (total_width - (int)title.length()) / 2;
// 
//     std::stringstream ss;
// 
//     VTB_LOG(INFO) << std::string(total_width, '=');
// 
//     ss.str(""); ss.clear();
//     ss << std::string(padding, ' ') << title;
//     VTB_LOG(INFO) << ss.str();
// 
//     VTB_LOG(INFO) << std::string(total_width, '=');
// 
//     // Headers now reflect a "Pair" per row
//     ss.str(""); ss.clear();
//     ss << std::left  << std::setw(6)  << "VID"
//        << std::setw(10) << "Q-PAIR"
//        << std::right << std::setw(16) << "TX Frames"
//        << std::setw(16) << "RX Frames"
//        << std::setw(15) << "TX Drp"
//        << std::setw(15) << "RX Drp";
//     VTB_LOG(INFO) << ss.str();
// 
//     VTB_LOG(INFO) << std::string(total_width, '-');
// 
//     bool activity_detected = false;
// 
//     for (int v = 0; v < MAX_VHOST_DEVICES; ++v) {
//         // Step by 2 to group RX/TX pairs
//         for (int q = 0; q < MAX_QUEUES; q += 2) {
// 
//             // Even QID (usually RX from backend perspective)
//             auto& s_even = stats_table_[v][q];
//             // Odd QID (usually TX from backend perspective)
//             auto& s_odd  = stats_table_[v][q + 1];
// 
//             uint64_t rx  = s_even.rx_frames.load(std::memory_order_relaxed);
//             uint64_t rxd = s_even.rx_dropped.load(std::memory_order_relaxed);
// 
//             uint64_t tx  = s_odd.tx_frames.load(std::memory_order_relaxed);
//             uint64_t txd = s_odd.tx_dropped.load(std::memory_order_relaxed);
// 
//             // Print if there is ANY activity in this pair
//             if (tx > 0 || rx > 0 || txd > 0 || rxd > 0) {
//                 activity_detected = true;
//                 ss.str(""); ss.clear();
// 
//                 // Represent the pair as "0/1", "2/3", etc.
//                 std::string pair_label = std::to_string(q) + "/" + std::to_string(q+1);
// 
//                 ss << std::left  << std::setw(6)  << v
//                    << std::setw(10) << pair_label
//                    << std::right << std::setw(16) << tx
//                    << std::setw(16) << rx
//                    << std::setw(15) << txd
//                    << std::setw(15) << rxd;
//                 VTB_LOG(INFO) << ss.str();
//             }
//         }
//     }
// 
//     if (!activity_detected) {
//         VTB_LOG(INFO) << "             No traffic processed across any Vhost devices.";
//     }
// 
//     VTB_LOG(INFO) << std::string(total_width, '=');
// }

// void ConfigManager::print_final_report() {
//     const int total_width = 85;
//     const std::string title = "VTB CONSOLIDATED PERFORMANCE REPORT";
//     int padding = (total_width - (int)title.length()) / 2;
// 
//     // Use a stringstream to format each line before passing to VTB_LOG
//     std::stringstream ss;
// 
//     // 1. Header
//     VTB_LOG(INFO) << std::string(total_width, '=');
//     
//     ss.str(""); ss.clear();
//     ss << std::string(padding, ' ') << title;
//     VTB_LOG(INFO) << ss.str();
//     
//     VTB_LOG(INFO) << std::string(total_width, '=');
// 
//     // 2. Column Headers
//     ss.str(""); ss.clear();
//     ss << std::left  << std::setw(8)  << "VID" 
//        << std::setw(8)  << "QID" 
//        << std::right << std::setw(16) << "TX Frames" 
//        << std::setw(17) << "RX Frames" 
//        << std::setw(17) << "TX Dropped" 
//        << std::setw(17) << "RX Dropped";
//     VTB_LOG(INFO) << ss.str();
//     
//     VTB_LOG(INFO) << std::string(total_width, '-');
// 
//     bool activity_detected = false;
// 
//     // 3. Data Rows
//     for (int v = 0; v < MAX_VHOST_DEVICES; ++v) {
//         for (int q = 0; q < MAX_QUEUES; ++q) {
//             auto& s = stats_table_[v][q];
//             uint64_t tx  = s.tx_frames.load(std::memory_order_relaxed);
//             uint64_t rx  = s.rx_frames.load(std::memory_order_relaxed);
//             uint64_t txd = s.tx_dropped.load(std::memory_order_relaxed);
//             uint64_t rxd = s.rx_dropped.load(std::memory_order_relaxed);
// 
//             if (tx > 0 || rx > 0 || txd > 0 || rxd > 0) {
//                 activity_detected = true;
//                 ss.str(""); ss.clear();
//                 ss << std::left  << std::setw(8)  << v 
//                    << std::setw(8)  << q 
//                    << std::right << std::setw(16) << tx 
//                    << std::setw(17) << rx 
//                    << std::setw(17) << txd 
//                    << std::setw(17) << rxd;
//                 VTB_LOG(INFO) << ss.str();
//             }
//         }
//     }
// 
//     if (!activity_detected) {
//         ss.str(""); ss.clear();
//         ss << std::string((total_width - 32) / 2, ' ') << "No traffic processed by any port.";
//         VTB_LOG(INFO) << ss.str();
//     }
// 
//     VTB_LOG(INFO) << std::string(total_width, '=');
// }

// void ConfigManager::print_final_report() {
//     const int total_width = 85;
//     const std::string title = "VTB CONSOLIDATED PERFORMANCE REPORT";
//     
//     // Calculate padding for manual centering
//     int padding = (total_width - title.length()) / 2;
// 
//     // 1. Table Header
//     std::cout << "\n" << std::string(total_width, '=') << "\n";
//     std::cout << std::string(padding, ' ') << title << "\n";
//     std::cout << std::string(total_width, '=') << "\n";
//     
//     // Column Definitions
//     std::cout << std::left  << std::setw(8)  << "VID" 
//               << std::setw(8)  << "QID" 
//               << std::right << std::setw(16) << "TX Frames" 
//               << std::setw(17) << "RX Frames" 
//               << std::setw(17) << "TX Dropped" 
//               << std::setw(17) << "RX Dropped" << "\n";
//     std::cout << std::string(total_width, '-') << "\n";
// 
//     bool activity_detected = false;
// 
//     // 2. Iterate through the common space
//     for (int v = 0; v < MAX_VHOST_DEVICES; ++v) {
//         for (int q = 0; q < MAX_QUEUES; ++q) {
//             
//             auto& s = stats_table_[v][q];
//             uint64_t tx  = s.tx_frames.load(std::memory_order_relaxed);
//             uint64_t rx  = s.rx_frames.load(std::memory_order_relaxed);
//             uint64_t txd = s.tx_dropped.load(std::memory_order_relaxed);
//             uint64_t rxd = s.rx_dropped.load(std::memory_order_relaxed);
// 
//             // 3. Only print rows with activity
//             if (tx > 0 || rx > 0 || txd > 0 || rxd > 0) {
//                 activity_detected = true;
//                 std::cout << std::left  << std::setw(8)  << v 
//                           << std::setw(8)  << q 
//                           << std::right << std::setw(16) << tx 
//                           << std::setw(17) << rx 
//                           << std::setw(17) << txd 
//                           << std::setw(17) << rxd << "\n";
//             }
//         }
//     }
// 
//     if (!activity_detected) {
//         std::cout << std::string((total_width - 32) / 2, ' ') 
//                   << "No traffic processed by any port.\n";
//     }
// 
//     std::cout << std::string(total_width, '=') << std::endl;
// }

}  // namespace vtb
