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

// Ctor
VhostController::VhostController(std::string socket_path)
    : socket_path_{std::move(socket_path)} {
   VhostController* expected = nullptr;

   if (!instance_.compare_exchange_strong(expected, this,
                                          std::memory_order_acq_rel)) {
      throw std::runtime_error(
          "Vhost Controller: Only one Vhost Controller instance allowed");
   }

   mode_ = vtb::ConfigManager::get_instance().get_arg<std::string>("-m");
   if (mode_ == "Loopback" || mode_ == "Back2Back") {
      create_client();
   }
}

VhostController::~VhostController() {
   vtb::info() << "Cleanup: Vhost Controller";

   if (driver_registered_) {
      rte_vhost_driver_unregister(socket_path_.c_str());
      driver_registered_ = false;
   }

   if (eal_initialised_) {
      rte_eal_cleanup();
      eal_initialised_ = false;
   }

   instance_.store(nullptr, std::memory_order_release);
}

// EAL initialisation
void VhostController::init(int argc, char* argv[]) {
   int ret = rte_eal_init(argc, argv);

   if (ret < 0) throw std::runtime_error("Vhost Controller: EAL init failed");

   eal_initialised_ = true;
}

// Register socket, negotiate features, start listening
void VhostController::start() {
   const char* path = socket_path_.c_str();

   if (rte_vhost_driver_register(path, 0) != 0) {
      throw std::runtime_error("Vhost Controller: Driver register failed: " +
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
      throw std::runtime_error("Vhost Controller: Callback register failed");
   }

   if (rte_vhost_driver_start(path) != 0) {
      throw std::runtime_error("Vhost Controller: Driver start failed");
   }
   vtb::info() << "Vhost Controller: Ready @" << path;
   vtb::info() << "Vhost Controller: waiting for guest...";
}

// run() — launches main on a lcore TODO
void VhostController::run() {
   // launch main to the first lcore
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
   vtb::info() << "Vhost Controller: New device added with VID: " << vid;

   int vring_count = rte_vhost_get_vring_num(vid);
   vtb::details() << "Vhost Controller: Total Number of queues for " << vid
                  << " is " << vring_count << " for with portnum "
                  << VhostController::port_cntr_;

   vtb::ConfigManager::get_instance().init_vhost_device(
         VhostController::port_cntr_, vid, (vring_count / 2));
   vtb::ConfigManager::get_instance().set_queue_state(vid, 0, 1);
   vtb::ConfigManager::get_instance().set_queue_state(vid, 1, 1);

   notify_port_controller(0, vid, 0, 1);
   notify_port_controller(0, vid, 1, 1);
   VhostController::port_cntr_++;
}

void VhostController::on_destroy_device(int vid) {
   vtb::info() << "Vhost Controller: Device with VID: " << vid << " removed";
   notify_port_controller(1, vid, 0, 0);
   vtb::ConfigManager::get_instance().clear_device(vid);
}

void VhostController::on_vring_state_changed(int vid, uint16_t queue_id,
                                             int enable) {
   vtb::details() << "Vhost Controller: vring state changed vid=" << vid
                  << " queue_id=" << queue_id << " enable=" << enable;

   vtb::ConfigManager::get_instance().set_queue_state(vid, queue_id, enable);

   if (queue_id >= 2) // 0 & 1 will be taken care by the notify in new device
      notify_port_controller(0, vid, queue_id, enable);
}

void VhostController::create_client() {
   abstract_sockname_ =
       vtb::ConfigManager::get_instance().get_arg<std::string>("-absn");
   std::string sock_path = std::string(1, '\0') + abstract_sockname_;
   vtb::details() << "Vhost Controler: Abstract Socket Name: "
                  << abstract_sockname_;

   if (sock_path.size() > 0 && sock_path[0] == '\0') {
      vtb::details() << "Verified: First byte is NULL.";
   }
   abstract_fd_ = vtb::create_client_socket(sock_path);
}

bool VhostController::notify_port_controller(int meta, int vid, uint16_t queue_id,
                                             int enable) {
   std::lock_guard<std::mutex> lock(notify_mutex_);
   if (mode_ == "Loopback" || mode_ == "Back2Back") {
      PortDeviceRingState pdrs = {meta, port_cntr_, vid, queue_id, enable};
      vtb::send_packet(abstract_fd_, pdrs);
   }
   return false;
}

}  // namespace vtb
