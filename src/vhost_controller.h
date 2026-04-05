#pragma once

#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_ring.h>
#include <rte_vhost.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "common.h"

namespace vtb {

#ifndef VIRTIO_NET_F_MRG_RXBUF
#define VIRTIO_NET_F_MRG_RXBUF 15
#endif

// RAII wrapper around DPDK vhost-user backend.
class VhostController {
public:
   explicit VhostController(std::string socket_path);
   virtual ~VhostController();

   // force single ownership
   VhostController(const VhostController&) = delete;
   VhostController& operator=(const VhostController&) = delete;

   // Initialise EAL
   void init(int argc, char* argv[]);

   // Register vhost-user socket, set features, begin listening.
   void start();

protected:
   // Device lifecycle hooks.
   virtual void on_new_device(int vid);
   virtual void on_destroy_device(int vid);
   virtual void on_vring_state_changed(int vid, uint16_t queue_id, int enable);

private:
   // Static C callbacks forwarded to the singleton instance.
   static int cb_new_device(int vid);
   static void cb_destroy_device(int vid);
   static int cb_vring_state_changed(int vid, uint16_t queue_id, int enable);
   bool notify_port_controller(int meta, int vid, uint16_t queue_id, int enable);
   void create_client();

   std::string socket_path_{};
   bool eal_initialised_{false};
   bool driver_registered_{false};
   static std::atomic<VhostController*> instance_;
   static int port_cntr_;
   std::string abstract_sockname_{};
   int abstract_fd_{-1};
   std::string mode_{};
   mutable std::mutex notify_mutex_;
};

}  // namespace vtb
