#include "vhost_controller.h"

#include <rte_log.h>
#include <unistd.h>

#include <cinttypes>
#include <csignal>
#include <cstdio>
#include <stdexcept>

#include "config_manager.h"
#include "logger.h"
#include "messenger.h"

namespace vtb {

#define RTE_LOGTYPE_VHDEV RTE_LOGTYPE_USER1

// Static instance pointer (supports one backend per process)
std::atomic<VhostController*> VhostController::instance_{nullptr};

int VhostController::port_cntr_ = 0;

// File scope only, not accessible outside this file
static vtb::ConfigManager& config = vtb::ConfigManager::get_instance();

// Ctor
VhostController::VhostController(std::string socket_path) : socket_path_{std::move(socket_path)} {
   VhostController* expected = nullptr;

   if (!instance_.compare_exchange_strong(expected, this, std::memory_order_acq_rel)) {
      throw std::runtime_error(
          "VhostController: Only one VhostController instance allowed");
   }

   mode_ = config.get_arg<std::string>("--mode");
   if (mode_ == "Loopback" || mode_ == "Back2Back") {
      VTB_LOG(DEBUG) << "VhostController: Tring to connect to Abstract socket server...";
      create_client();
      VTB_LOG(DEBUG) << "VhostController: Connected to Abstract socket server...";
   }
}

VhostController::~VhostController() {
   VTB_LOG(INFO) << "VhostController: Cleaning up...";

   if (driver_registered_) {
      rte_vhost_driver_unregister(socket_path_.c_str());
      driver_registered_ = false;
   }
   if (eal_initialised_) {
      // rte_eal_cleanup();
      // fires a segfault if i try to clean the mbufpool and rings manually in
      // the handler, hence commented
      eal_initialised_ = false;
   }
   instance_.store(nullptr, std::memory_order_release);
}

// EAL initialisation
void VhostController::init(int argc, char* argv[]) {
   int ret = rte_eal_init(argc, argv);

   if (ret < 0) 
      throw std::runtime_error("VhostController: EAL init failed");

   eal_initialised_ = true;
}

// Register socket, negotiate features, start listening
void VhostController::start() {
   const char* path = socket_path_.c_str();

   if (rte_vhost_driver_register(path, 0) != 0) {
      throw std::runtime_error("VhostController: Driver register failed: " +
                               socket_path_);
   }
   driver_registered_ = true;

   // rte_vhost_driver_disable_features(path, 1ULL << VIRTIO_NET_F_MQ); // CHECK
   rte_vhost_driver_enable_features(path, 1ULL << VIRTIO_NET_F_MRG_RXBUF);

   static const struct rte_vhost_device_ops ops = {
       .new_device = cb_new_device,
       .destroy_device = cb_destroy_device,
       .vring_state_changed = cb_vring_state_changed,
       .features_changed = nullptr,
       .new_connection = nullptr,
       .destroy_connection = nullptr,
       .guest_notified = nullptr,
       .guest_notify = nullptr};

   if (rte_vhost_driver_callback_register(path, &ops) != 0) {
      throw std::runtime_error("VhostController: Callback register failed");
   }

   if (rte_vhost_driver_start(path) != 0) {
      throw std::runtime_error("VhostController: Driver start failed");
   }
   VTB_LOG(INFO) << "VhostController: Ready @" << path;
   VTB_LOG(INFO) << "VhostController: Waiting for guest to start...";
}

//------------------------------------------------------------------
// Static C callbacks -> instance dispatch
//------------------------------------------------------------------
int VhostController::cb_new_device(int vid) {
   instance_.load(std::memory_order_acquire)->on_new_device(vid);
   return 0;
}

void VhostController::cb_destroy_device(int vid) {
   instance_.load(std::memory_order_acquire)->on_destroy_device(vid);
}

int VhostController::cb_vring_state_changed(int vid, uint16_t queue_id,
                                            int enable) {
   instance_.load(std::memory_order_acquire)
       ->on_vring_state_changed(vid, queue_id, enable);
   return 0;
}

//------------------------------------------------------------------
// Device lifecycle hooks
//------------------------------------------------------------------
void VhostController::on_new_device(int vid) {
   VTB_LOG(INFO) << "VhostController: New port added with VID: " << vid;

   int vring_count = rte_vhost_get_vring_num(vid); // TODO return value is
                                                   // always 8, does nto reflect
                                                   // the correct number of
                                                   // queues for a given port
   VTB_LOG(DEBUG) << "VhostController: Total Number of queues for " << vid
                  << " is " << vring_count << " for with portnum "
                  << VhostController::port_cntr_;

   config.init_vhost_device(VhostController::port_cntr_, vid, (vring_count / 2));
   config.set_queue_state(vid, 0, 1);
   config.set_queue_state(vid, 1, 1);

   notify_port_controller(0, vid, 0, 1);
   notify_port_controller(0, vid, 1, 1);
   VhostController::port_cntr_ += 1;
}

void VhostController::on_destroy_device(int vid) {
   VTB_LOG(INFO) << "VhostController: Port with VID: " << vid << " removed";
   notify_port_controller(1, vid, 0, 0);
   config.clear_device(vid);
}

void VhostController::on_vring_state_changed(int vid, uint16_t queue_id,
                                             int enable) {
   VTB_LOG(DEBUG) << "VhostController: vring state changed vid=" << vid
                  << " queue_id=" << queue_id << " enable=" << enable;

   config.set_queue_state(vid, queue_id, enable);

   if (queue_id >= 2) // 0 & 1 will be taken care by the notify in new device
      notify_port_controller(0, vid, queue_id, enable);
}

void VhostController::create_client() {
   abstract_sockname_ = config.get_arg<std::string>("--abstract-sockname");

   std::string sock_path = std::string(1, '\0') + abstract_sockname_;

   VTB_LOG(DEBUG) << "VhostControler: Abstract Socket Name: "
                  << abstract_sockname_;

   if (sock_path.size() > 0 && sock_path[0] == '\0') {
      VTB_LOG(DEBUG) << "VhostController: Verified: Abstract socket has first byte is NULL.";
   }

   abstract_fd_ = vtb::create_client_socket(sock_path);

   VTB_LOG(DEBUG) << "VhostController: Abstract Socket Create: " << abstract_fd_;
}

bool VhostController::notify_port_controller(int meta, int vid, uint16_t queue_id,
                                             int enable) {
   std::lock_guard<std::mutex> lock(notify_mutex_);

   if (mode_ == "Loopback" || mode_ == "Back2Back") {

      PortDeviceRingState pdrs = {meta, port_cntr_, vid, queue_id, enable};

      if (abstract_fd_ != -1) {
         vtb::send_packet(abstract_fd_, pdrs);
      } else {
         VTB_LOG(ERROR) << "VhostController: Writing to an Uninitialized Abstract socket" << abstract_fd_;
      }
   } 
   // TODO port controller for emu needs to close their connections
   return false;
}

}  // namespace vtb
