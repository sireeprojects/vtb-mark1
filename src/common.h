#pragma once

#include <unistd.h>
#include <cstdint>
#include <memory>
#include <string_view>
#include <thread>
#include <sstream>
#include <vector>
#include <string>

#include "messenger.h"

namespace vtb {

static constexpr int MAX_QUEUE_PAIRS = 8;
static constexpr uint32_t MBUF_POOL_SIZE       = 8191;
static constexpr uint32_t MBUF_CACHE_SIZE      = 256;
static constexpr uint16_t PKT_BURST_SZ         = 32;
static constexpr uint16_t VIRTIO_RXQ           = 0;
static constexpr uint16_t VIRTIO_TXQ           = 1;
static constexpr uint32_t RING_SIZE            = 4096;  // must be power-of-2
static constexpr uint32_t MAX_ENQUEUE_RETRIES  = 1000;

enum class VhostNotifyMetadata {
   PORT_UP = 0,
   PORT_DOWN = 1
};

struct VhostQueuePair {
   uint16_t rxq_id;
   uint16_t txq_id;
   bool rxq_enabled;
   bool txq_enabled;
};

struct VhostDevice {
   int nof_queue_pairs;
   int qpid[MAX_QUEUE_PAIRS];
   VhostQueuePair qp[MAX_QUEUE_PAIRS];
   bool ready;
   uint16_t ctlq_id;
};

struct PortQueuePair {
   int rxq_id;
   int txq_id;
};

struct PortDevice {
   PortQueuePair qp[MAX_QUEUE_PAIRS];
   int ctlq_id;
};

struct PortMap {
   VhostDevice vd;
   PortDevice pd;
};

struct PortDeviceEnables {
   int port_id;
   int device_id;
   int rxdq_id;
   int txdq_id;
};

struct PortDeviceRingState {
   VhostNotifyMetadata meta;
   int pid;
   int vid;
};

enum class ThreadMode {
   EachQTwoThread,
   EachQOneThread,
   AllQTwoThread,
   AllQOneThread
};

class PortController;

// The factory function declaration
std::unique_ptr<PortController> create_controller(std::string_view mode);

int create_server_socket(const std::string& path);
int create_client_socket(const std::string& path);
void set_thread_name(std::thread& th, const std::string& name);
void restore_echoctl();
void disable_echoctl();
void graceful_exit();
bool is_even(int n);
bool is_odd(int n);

template <typename T>
bool send_packet(int fd, const T& data) {
   ssize_t bytes_sent = write(fd, &data, sizeof(T));

   if (bytes_sent == -1) {
      perror("write");
      return false;
   } else if (static_cast<size_t>(bytes_sent) < sizeof(T)) {
      // Handle partial writes if necessary for large buffers
      VTB_LOG(INFO) << "Warning: Partial write occurred";
      return false;
   }
   return true;
}

std::string format_qids(const std::vector<int>& vec);

std::string demangle(const char* name);

ThreadMode string_to_thread_mode(std::string_view mode_str);

}  // namespace vtb
