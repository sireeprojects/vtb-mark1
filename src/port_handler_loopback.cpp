#include <rte_errno.h>

#include "port_handler_loopback.h"

namespace vtb {

static vtb::ConfigManager& config = vtb::ConfigManager::get_instance();   

PortHandlerLoopback::~PortHandlerLoopback() {
   shutdown();
}

void PortHandlerLoopback::shutdown() {
   is_running_ = false;
   for (std::thread& t : worker_threads_) {
      if (t.joinable()) {
         t.join();
      }
   }
   // TODO free mempools
   //      free rings
}

void PortHandlerLoopback::dequeue_tx_packets(int vid, int qid) {
   struct rte_mbuf* pkts[vtb::PKT_BURST_SZ];
   uint16_t nb_tx = rte_vhost_dequeue_burst(
         vid, 
         qid, 
         txq_mbuf_pool_, 
         pkts, 
         PKT_BURST_SZ);

   if (nb_tx == 0) {
      return;
   }

	// Lossless enqueue into ring — retry until all are in
	uint16_t sent = 0;
	while (sent < nb_tx) {
	    unsigned int pushed = rte_ring_sp_enqueue_burst(
	        txq_ring_,
	        reinterpret_cast<void**>(&pkts[sent]),
	        nb_tx - sent,
	        nullptr);
	    sent += pushed;
	    if (sent < nb_tx)
	        usleep(1);  // backpressure: ring full, let consumer drain
	}
	txq_pkt_cnt_ += sent;
   VTB_LOG(INFO) << "PortHandlerLoopback: Vhost Dequeued: " 
      << nb_tx << " Ring Enqueued: " << txq_pkt_cnt_ ;
}

void PortHandlerLoopback::enqueue_rx_packets(int vid, int qid) {
	struct rte_mbuf* pkts[PKT_BURST_SZ];
	unsigned int nb_deq = rte_ring_sc_dequeue_burst(
	    txq_ring_,
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
	    uint16_t nb_tx = rte_vhost_enqueue_burst(vid, qid, &pkts[sent], nb_deq - sent);
	    sent += nb_tx;
	    if (sent < nb_deq) {
	        retries++;
	        usleep(1); // backpressure: guest RX ring full
	    }
	}

	rxq_pkt_cnt_ += sent;

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
	        txq_ring_, reinterpret_cast<void**>(pkts), PKT_BURST_SZ, nullptr);
	    for (unsigned int i = 0; i < remaining; i++)
	        rte_pktmbuf_free(pkts[i]);
	} while (remaining > 0);

   VTB_LOG(INFO) << "PortHandlerLoopback: No of pkts Enqueued to Guest: " << rxq_pkt_cnt_;
}

ThreadMode PortHandlerLoopback::string_to_thread_mode(std::string_view mode_str) {
   if (mode_str == "EachQ-TwoThread") {
      return ThreadMode::EachQTwoThread;
   }
   if (mode_str == "EachQ-OneThread") {
      return ThreadMode::EachQOneThread;
   }
   if (mode_str == "AllQ-TwoThread") {
      return ThreadMode::AllQTwoThread;
   }
   if (mode_str == "AllQ-OneThread") {
      return ThreadMode::AllQOneThread;
   }

   throw std::invalid_argument("Invalid thread mode string: " + std::string(mode_str));
}

void PortHandlerLoopback::start([[maybe_unused]] int pid, int vid) {
   auto threading_mode = config.get_arg<std::string>("--threading-mode");

   VidContext ctx = {};
   ctx.vid = vid;
   ctx.qids = get_queue_ids_by_vid(vid);

   dispatch(ctx, threading_mode);
}

void PortHandlerLoopback::worker(VidContext ctx) {
   int vid = ctx.vid;
   std::vector<int>qids = ctx.qids;

   // TODO create mempool and rings here

   while (is_running_) {
      for (unsigned int id=0; id<qids.size(); id++) {
         if (id & 1) { // CHECK
            dequeue_tx_packets(vid, qids[id]);
         } else {
            enqueue_rx_packets(vid, qids[id]);
         }
      }
   }

   // TODO free mempool and rings here
}

void PortHandlerLoopback::launch(VidContext ctx) {
   worker_threads_.emplace_back(&PortHandlerLoopback::worker, this, ctx);
   std::thread& last_thread = worker_threads_.back();
   std::string worker_name = "WorkerV" + std::to_string(ctx.vid);
   vtb::set_thread_name(last_thread, worker_name);
}

void PortHandlerLoopback::dispatch(const VidContext& context, const std::string& mode_str) {
   int vid = context.vid;
   std::vector<int>qids = context.qids;

   ThreadMode mode = string_to_thread_mode(mode_str);

   switch (mode) {
      case ThreadMode::EachQTwoThread: // indepented threads
         for (int id=0; id<static_cast<int>(qids.size()); id++) {
            launch({vid, {id}});
         }
         break;

      case ThreadMode::EachQOneThread: // on thread per tx rx pair
         for (int id=0; id<static_cast<int>(qids.size()); id+=2) {
            launch({vid, {id, id+1}});
         }
         break;

      case ThreadMode::AllQTwoThread: {
         size_t mid = qids.size() / 2;
         std::vector<int> tx_qids(qids.begin(), qids.begin() + mid);
         std::vector<int> rx_qids(qids.begin() + mid, qids.end());
         launch({vid, tx_qids});
         launch({vid, rx_qids});
         break;
      }

      case ThreadMode::AllQOneThread:
         launch({vid, qids});
         break;

      default:
         // This handles the unlikely case of an unhandled enum value
         break;
   }
}

void PortHandlerLoopback::dequeue_tx_packets() {
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

void PortHandlerLoopback::enqueue_rx_packets() {
}

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

} // namespace vtb

/*
void PortHandlerLoopback::start() {

   // start the appropriate port controller
   auto mode = vtb::ConfigManager::get_instance().get_arg<std::string>("--mode");
   auto threading_mode = vtb::ConfigManager::get_instance().get_arg<std::string>("--threading-mode");

   // "EachQ-TwoThread | EachQ-OneThread | AllQ-TwoThread | AllQ-OneThread",

   VTB_LOG(DEBUG) 
      << "PortHandlerLoopback: Starting thread"
      << " VID: " << vid
      << " RXQ: " << rxqid
      << " TXQ: " << txqid
      << " Mode: " << mode
      << " ThreadMode: " << threading_mode;

   // unique names for pools and rings
   std::string tx_pool_name = "TX_MBUF_POOL_" + 
                              std::to_string(vid) + "_" +
                              std::to_string(txqid);

   std::string rx_pool_name = "RX_MBUF_POOL_" +
                              std::to_string(vid) + "_" +
                              std::to_string(rxqid);

   std::string txq_ring_name = "TX_RING_" + 
                              std::to_string(vid) + "_" +
                              std::to_string(txqid);

   // create txq_mbuf_pool_
   VTB_LOG(DEBUG) << "PortHandlerLoopback: Creating Transmit MBUF Pool: " 
                  << tx_pool_name;

   txq_mbuf_pool_ = rte_pktmbuf_pool_create(tx_pool_name.c_str(),
                   MBUF_POOL_SIZE,
                   MBUF_CACHE_SIZE,
                   0,
                   RTE_MBUF_DEFAULT_BUF_SIZE,
                   rte_socket_id());

   if (!txq_mbuf_pool_)
      throw std::runtime_error("PortHandlerLoopback: Cannot create TX MBUF Pool: "
                               + tx_pool_name + " rte_errno:" +
                               std::to_string(rte_errno));
   
   // create rxq_mbuf_pool_
   VTB_LOG(DEBUG) << "PortHandlerLoopback: Creating Receive MBUF Pool: "
                  << rx_pool_name;

   rxq_mbuf_pool_ = rte_pktmbuf_pool_create(rx_pool_name.c_str(),
                   MBUF_POOL_SIZE,
                   MBUF_CACHE_SIZE,
                   0,
                   RTE_MBUF_DEFAULT_BUF_SIZE,
                   rte_socket_id());

   if (!rxq_mbuf_pool_)
      throw std::runtime_error("PortHandlerLoopback: Cannot create RX MBUF Pool: "
                               + rx_pool_name + " rte_errno:" +
                               std::to_string(rte_errno));
   
   // create txq_ring_
   VTB_LOG(DEBUG) << "PortHandlerLoopback: Creating Ring: " << txq_ring_name;

   txq_ring_ = rte_ring_create(txq_ring_name.c_str(),
                  RING_SIZE, 
                  rte_socket_id(),
                  RING_F_SP_ENQ | RING_F_SC_DEQ);

   if (!txq_ring_)
       throw std::runtime_error(
             "PortHandlerLoopback: Cannot create inter-thread ring: "
             + txq_ring_name + " rte_errno:" +
             std::to_string(rte_errno));

   // Select threading model

   if (mode=="Loopback") {
      if (threading_mode == "EachQ-OneThread") {

         VTB_LOG(DEBUG) 
            << "PortHandlerLoopback: Inside Loopback mode, Single thread mode"
            << " VID: " << vid
            << " RXQ: " << rxqid
            << " TXQ: " << txqid;

         rxq_ring_ = txq_ring_;
         txq_rxq_thread_ = std::thread(&PortHandlerLoopback::txq_rxq_worker, this);
         vtb::set_thread_name(txq_rxq_thread_, txq_ring_name);
      }
      else if (threading_mode == "EachQ-TwoThread") {

         VTB_LOG(DEBUG) 
            << "PortHandlerLoopback: Inside Loopback mode, Two thread mode"
            << " VID: " << vid
            << " RXQ: " << rxqid
            << " TXQ: " << txqid;

         rxq_ring_ = txq_ring_;
         txq_thread_ = std::thread(&PortHandlerLoopback::txq_worker, this);
         rxq_thread_ = std::thread(&PortHandlerLoopback::rxq_worker, this);
      }
   }
}

void PortHandlerLoopback::dequeue_tx_packets() {
   struct rte_mbuf* pkts[vtb::PKT_BURST_SZ];
   uint16_t nb_tx = rte_vhost_dequeue_burst(
         vid, 
         VIRTIO_TXQ, 
         txq_mbuf_pool_, 
         pkts, 
         PKT_BURST_SZ);

   if (nb_tx == 0) {
      return;
   }

	// Lossless enqueue into ring — retry until all are in
	uint16_t sent = 0;
	while (sent < nb_tx) {
	    unsigned int pushed = rte_ring_sp_enqueue_burst(
	        txq_ring_,
	        reinterpret_cast<void**>(&pkts[sent]),
	        nb_tx - sent,
	        nullptr);
	    sent += pushed;
	    if (sent < nb_tx)
	        usleep(1);  // backpressure: ring full, let consumer drain
	}
	txq_pkt_cnt_ += sent;
   VTB_LOG(INFO) << "PortHandlerLoopback: Vhost Dequeued: " 
      << nb_tx << " Ring Enqueued: " << txq_pkt_cnt_ ;
}

void PortHandlerLoopback::enqueue_rx_packets() {
	struct rte_mbuf* pkts[PKT_BURST_SZ];
	unsigned int nb_deq = rte_ring_sc_dequeue_burst(
	    txq_ring_,
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

	rxq_pkt_cnt_ += sent;

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
	
	// Drain any remaining mbufs in the ring so they are not leaked
	unsigned int remaining;
	do {
	    struct rte_mbuf* pkts[PKT_BURST_SZ];
	    remaining = rte_ring_sc_dequeue_burst(
	        txq_ring_, reinterpret_cast<void**>(pkts), PKT_BURST_SZ, nullptr);
	    for (unsigned int i = 0; i < remaining; i++)
	        rte_pktmbuf_free(pkts[i]);
	} while (remaining > 0);

   VTB_LOG(INFO) << "PortHandlerLoopback: No of pkts Enqueued to Guest: " << rxq_pkt_cnt_;
}
 
*/
