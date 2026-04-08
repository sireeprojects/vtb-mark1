#pragma once

#include <atomic>
#include <thread>
#include <stdexcept>
#include <cstdint>
#include <unistd.h>
#include <rte_mbuf.h>
#include <rte_vhost.h>
#include "messenger.h"
#include "config_manager.h"

namespace vtb {
    static constexpr uint32_t MBUF_POOL_SIZE       = 8191;
    static constexpr uint32_t MBUF_CACHE_SIZE      = 256;
    static constexpr uint16_t PKT_BURST_SZ         = 32;
    static constexpr uint16_t VIRTIO_RXQ           = 0;
    static constexpr uint16_t VIRTIO_TXQ           = 1;
    static constexpr uint32_t RING_SIZE            = 4096;  // must be power-of-2
    static constexpr uint32_t MAX_ENQUEUE_RETRIES  = 1000;

// Abstract base class (interface) for port handling.
// TX and RX pipelines run on dedicated threads.
class PortHandler {
public:
   PortHandler() = default;
   virtual ~PortHandler() = default;

   // block copies
   PortHandler(const PortHandler&) = delete;
   PortHandler& operator=(const PortHandler&) = delete;

   // Starts both transmit and receive threads.
   virtual void start() = 0;

   // Signals both threads to stop.
   void stop();
   void set_ids(int devid, int rqid, int tqid);

protected:
   // TX pipeline
   virtual void dequeue_tx_packets() = 0;
   virtual void extract_tx_metadata() = 0;
   virtual void decode_tx_metadata() = 0;
   virtual void act_on_tx_metadata() = 0;
   virtual void create_tx_port_metadata() = 0;
   virtual void write_tx_packets() = 0;

   // RX pipeline
   virtual void read_rx_packets() = 0;
   virtual void extract_rx_metadata() = 0;
   virtual void decode_rx_metadata() = 0;
   virtual void act_on_rx_metadata() = 0;
   virtual void create_rx_vm_metadata() = 0;
   virtual void enqueue_rx_packets() = 0;

   // workers
   virtual void txq_worker() = 0;
   virtual void rxq_worker() = 0;
   virtual void txq_rxq_worker() = 0;

   std::atomic<bool> is_running_{false};

   int vid{-1};
   int rxqid{-1};
   int txqid{-1};

   // used in two thread model only
   // used in both loopback/back2back model
   std::thread txq_thread_;
   std::thread rxq_thread_;

   // used in single thread model only
   std::thread txq_rxq_thread_;

   // used in both single/two thread model
   // used in both loopback/back2back model
   struct rte_mempool *txq_mbuf_pool_{nullptr};
   struct rte_mempool *rxq_mbuf_pool_{nullptr};

   // used in back2back model only
   struct rte_ring *txq_ring_{nullptr};
   struct rte_ring *rxq_ring_{nullptr};

   // statistics counters
   uint64_t txq_pkt_cnt_{0};
   uint64_t rxq_pkt_cnt_{0};
   // TODO add if any other data needs to be captured

   // performance statistics
   // TODO
};

} // namespace vtb
