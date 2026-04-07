#include <rte_errno.h>

#include "port_handler_loopback.h"

namespace vtb {

PortHandlerLoopback::~PortHandlerLoopback() {
   if (tx_thread_.joinable())
      tx_thread_.join();
   if (rx_thread_.joinable())
      rx_thread_.join();
   if (tx_rx_thread_.joinable())
      tx_rx_thread_.join();

   VTB_LOG(DEBUG) << "PortHandlerLoopback: destructor: "
                  << " vid: " << vid
                  << " rxqid: " << rxqid
                  << " txqid: " << txqid;
   if (tx_mbuf_pool_) { rte_mempool_free(tx_mbuf_pool_); tx_mbuf_pool_ = nullptr; }
   if (rx_mbuf_pool_) { rte_mempool_free(rx_mbuf_pool_); rx_mbuf_pool_ = nullptr; }
   if (tx_ring_) { rte_ring_free(tx_ring_); tx_ring_ = nullptr; }
   rx_ring_ = nullptr;
}

void PortHandlerLoopback::start() {

   // start the appropriate port controller
   auto mode = vtb::ConfigManager::get_instance().get_arg<std::string>("--mode");
   auto nof_threads = vtb::ConfigManager::get_instance().get_arg<int>("--txrx-threads");

   VTB_LOG(DEBUG) << "PortHandlerLoopback: Starting thread"
               << " VID: " << vid
               << " RXQ: " << rxqid
               << " TXQ: " << txqid
               << " Mode: " << mode
               << " ThreadMode: " << nof_threads;

   // unique names for pools and rings
   std::string tx_pool_name = "TX_MBUF_POOL_" + std::to_string(vid) + "_" + std::to_string(txqid);
   std::string rx_pool_name = "RX_MBUF_POOL_" + std::to_string(vid) + "_" + std::to_string(rxqid);
   std::string tx_ring_name = "TX_RING_"      + std::to_string(vid) + "_" + std::to_string(txqid);

   // create tx_mbuf_pool_
   VTB_LOG(DEBUG) << "PortHandlerLoopback: Creating Transmit MBUF Pool: " << tx_pool_name;
   tx_mbuf_pool_ = rte_pktmbuf_pool_create(tx_pool_name.c_str(),
                   MBUF_POOL_SIZE,
                   MBUF_CACHE_SIZE,
                   0,
                   RTE_MBUF_DEFAULT_BUF_SIZE,
                   rte_socket_id());
   if (!tx_mbuf_pool_)
      throw std::runtime_error("PortHandlerLoopback: Cannot create TX MBUF Pool: " + tx_pool_name + " rte_errno:" + std::to_string(rte_errno));
   
   // create rx_mbuf_pool_
   VTB_LOG(DEBUG) << "PortHandlerLoopback: Creating Receive MBUF Pool: " << rx_pool_name;
   rx_mbuf_pool_ = rte_pktmbuf_pool_create(rx_pool_name.c_str(),
                   MBUF_POOL_SIZE,
                   MBUF_CACHE_SIZE,
                   0,
                   RTE_MBUF_DEFAULT_BUF_SIZE,
                   rte_socket_id());
   if (!rx_mbuf_pool_)
      throw std::runtime_error("PortHandlerLoopback: Cannot create RX MBUF Pool: " + rx_pool_name + " rte_errno:" + std::to_string(rte_errno));
   
   // create tx_ring_
   /* SP/SC ring: single-producer (dequeue thread), single-consumer (enqueue thread) */
   VTB_LOG(DEBUG) << "PortHandlerLoopback: Creating Ring: " << tx_ring_name;
   tx_ring_ = rte_ring_create(tx_ring_name.c_str(),
                  RING_SIZE, 
                  rte_socket_id(),
                  RING_F_SP_ENQ | RING_F_SC_DEQ);
   if (!tx_ring_)
       throw std::runtime_error("PortHandlerLoopback: Cannot create inter-thread ring: " + tx_ring_name + " rte_errno:" + std::to_string(rte_errno));

   //----------------------------------------

   if (mode=="Loopback") {
      if (nof_threads == 1) {
         VTB_LOG(DEBUG) << "PortHandlerLoopback: Inside Loopback mode, Single thread mode"
               << " VID: " << vid
               << " RXQ: " << rxqid
               << " TXQ: " << txqid;
         // assign tx_ring_ to rx_ring_
         rx_ring_ = tx_ring_;
         // start tx_rx_thread_
         tx_rx_thread_ = std::thread(&PortHandlerLoopback::tx_rx_worker, this);
         vtb::set_thread_name(tx_rx_thread_, tx_ring_name);
         // tx_rx_thread_.detach(); // CHECK: results in a segfault
      }
      else if (nof_threads == 2) {
         VTB_LOG(DEBUG) << "PortHandlerLoopback: Inside Loopback mode, Two thread mode"
               << " VID: " << vid
               << " RXQ: " << rxqid
               << " TXQ: " << txqid;
         // assign tx_ring_ to rx_ring_
         rx_ring_ = tx_ring_;
         // start tx_thread_
         tx_thread_ = std::thread(&PortHandlerLoopback::tx_worker, this);
         tx_thread_.detach();
         // start rx_thread_
         rx_thread_ = std::thread(&PortHandlerLoopback::rx_worker, this);
         rx_thread_.detach();
      }
   }
}

// --- Worker Thread Definitions ---

void PortHandlerLoopback::tx_worker() {
   // TODO
   VTB_LOG(INFO) << "PortHandlerLoopback: tx_worker started";
   is_running_ = true;
   while (is_running_) {
      VTB_LOG(INFO) << "TX running: " << is_running_;
      sleep(1);
   }
   VTB_LOG(INFO) << "PortHandlerLoopback: tx_worker terminated";
}

void PortHandlerLoopback::rx_worker() {
   // TODO
   VTB_LOG(INFO) << "PortHandlerLoopback: rx_worker started";
   is_running_ = true;
   while (is_running_) {
      VTB_LOG(INFO) << "RX running: " << is_running_;
      sleep(1);
   }
   VTB_LOG(INFO) << "PortHandlerLoopback: rx_worker terminated";
}

void PortHandlerLoopback::tx_rx_worker() {
   VTB_LOG(INFO) << "PortHandlerLoopback: Queue worker started for"
               << " VID: " << vid
               << " RXQ: " << rxqid
               << " TXQ: " << txqid;
   is_running_ = true;
   while (is_running_) {
      dequeue_tx_packets();
      enqueue_rx_packets();
      // VTB_LOG(INFO) << "PortHandlerLoopback: TX_RX running:"
      //    << " VID: " << vid
      //    << " RQID: " << rxqid
      //    << " TQID: " << txqid;
      sleep(1);
   }
   VTB_LOG(INFO) << "PortHandlerLoopback: Queue worker stopped for"
               << " VID: " << vid
               << " RXQ: " << rxqid
               << " TXQ: " << txqid;
   // TODO this is the right place to cleanup memories
   // because after the thread is done there is no need for these memories
   // no need to wait for destructors
   // 
   // BUT if the same vid tries to re-connect, then we will not have these
   // memories, start() needs to be called again
   // 
   // So if the plan is to recycle the handler for re-connections then it is
   // best to destroy the handler and create new ones on re-connection requests
   // much easier and less prone
   //
   // VTB_LOG(DEBUG) << "PortHandlerLoopback: Cleaning memories: "
   //                << " vid: " << vid
   //                << " rxqid: " << rxqid
   //                << " txqid: " << txqid;
   // if (tx_mbuf_pool_) { rte_mempool_free(tx_mbuf_pool_); tx_mbuf_pool_ = nullptr; }
   // if (rx_mbuf_pool_) { rte_mempool_free(rx_mbuf_pool_); rx_mbuf_pool_ = nullptr; }
   // if (tx_ring_) { rte_ring_free(tx_ring_); tx_ring_ = nullptr; }
   // rx_ring_ = nullptr;
}

void PortHandlerLoopback::dequeue_tx_packets() {
   struct rte_mbuf* pkts[vtb::PKT_BURST_SZ];
   uint16_t nb_tx = rte_vhost_dequeue_burst(
         vid, 
         VIRTIO_TXQ, 
         tx_mbuf_pool_, 
         pkts, 
         PKT_BURST_SZ);

   if (nb_tx == 0) {
      return;
   }

	// Lossless enqueue into ring — retry until all are in
	uint16_t sent = 0;
	while (sent < nb_tx) {
	    unsigned int pushed = rte_ring_sp_enqueue_burst(
	        tx_ring_,
	        reinterpret_cast<void**>(&pkts[sent]),
	        nb_tx - sent,
	        nullptr);
	    sent += pushed;
	    if (sent < nb_tx)
	        usleep(1);  // backpressure: ring full, let consumer drain
	}
	tx_pkt_cnt_ += sent;
   VTB_LOG(INFO) << "PortHandlerLoopback: Vhost Dequeued: " 
      << nb_tx << " Ring Enqueued: " << sent ;
}

void PortHandlerLoopback::extract_tx_metadata() {
}

void PortHandlerLoopback::decode_tx_metadata() {
}

void PortHandlerLoopback::act_on_tx_metadata() {
}

void PortHandlerLoopback::create_tx_port_metadata() {
}

void PortHandlerLoopback::write_tx_packets() {
}

// --- RX Pipeline Definitions ---

void PortHandlerLoopback::read_rx_packets() {
}

void PortHandlerLoopback::extract_rx_metadata() {
}

void PortHandlerLoopback::decode_rx_metadata() {
}

void PortHandlerLoopback::act_on_rx_metadata() {
}

void PortHandlerLoopback::create_rx_vm_metadata() {
}

void PortHandlerLoopback::enqueue_rx_packets() {
	struct rte_mbuf* pkts[PKT_BURST_SZ];
	unsigned int nb_deq = rte_ring_sc_dequeue_burst(
	    tx_ring_,
	    reinterpret_cast<void**>(pkts),
	    PKT_BURST_SZ,
	    nullptr);
	
	if (nb_deq == 0) {
	    return;
	}

	// Lossless enqueue to guest RX — retry until all accepted
	uint16_t sent = 0;
	uint32_t retries = 0;
	while (sent < nb_deq && retries < MAX_ENQUEUE_RETRIES) {
	    uint16_t nb_tx = rte_vhost_enqueue_burst(vid, VIRTIO_RXQ, &pkts[sent], nb_deq - sent);
	    sent += nb_tx;
	    if (sent < nb_deq) {
	        retries++;
	        usleep(1); // backpressure: guest RX ring full
	    }
	}

	rx_pkt_cnt_ += sent;

	if (sent < nb_deq) {
		VTB_LOG(ERROR) << "PortHandlerLoopback: Enqueue Failed " 
	      << "Pkts Dropped: " << MAX_ENQUEUE_RETRIES
			<< " After: " << nb_deq - sent << "retries";

	    // Free the mbufs that could not be delivered
	    for (unsigned int i = sent; i < nb_deq; i++)
	        rte_pktmbuf_free(pkts[i]); 
	}

	// Free delivered mbufs (vhost enqueue copies data)
	for (uint16_t i = 0; i < sent; i++)
	    rte_pktmbuf_free(pkts[i]);
	
	// Drain any remaining mbufs in the ring so they are not leaked */
	unsigned int remaining;
	do {
	    struct rte_mbuf* pkts[PKT_BURST_SZ];
	    remaining = rte_ring_sc_dequeue_burst(
	        tx_ring_, reinterpret_cast<void**>(pkts), PKT_BURST_SZ, nullptr);
	    for (unsigned int i = 0; i < remaining; i++)
	        rte_pktmbuf_free(pkts[i]);
	} while (remaining > 0);

   VTB_LOG(INFO) << "PortHandlerLoopback: No of pkts Enqueued to Guest: " << rx_pkt_cnt_;
}

} // namespace vtb
