#include <rte_errno.h>

#include "port_handler_loopback.h"

namespace vtb {

// file only config
static vtb::ConfigManager& config = vtb::ConfigManager::get_instance();   

PortHandlerLoopback::~PortHandlerLoopback() {
   shutdown();
}

void PortHandlerLoopback::shutdown() {
   // stop threading loop
   is_running_ = false;

   // wait for threads to finish
   for (std::thread& t : worker_threads_) {
      if (t.joinable()) {
         t.join();
      }
   }
   worker_threads_.clear();

   // free mempools and rings used by threads
   for (auto const& [qid, ptr] : mempools_) {
      VTB_LOG(DEBUG) << "PortHandlerLoopback: Shutdown: mempool free: " << qid;
      if (ptr) 
         rte_mempool_free(ptr);
   }
   mempools_.clear();

   for (auto const& [qid, ptr] : rings_) {
      VTB_LOG(DEBUG) << "PortHandlerLoopback: Shutdown: ring free: " << qid;
      if (ptr) 
         rte_ring_free(ptr);
   }
   rings_.clear();
}

void PortHandlerLoopback::create_resources(const int pid, const std::vector<int>& qids) {
   for (int qid : qids) {
      // Logic: Only create resources if the ID is odd (Transmit Queue)
      if (qid % 2 != 0) {
         // 1. Generate unique names for DPDK visibility
         std::string mp_name = "mp_tx_q"     + std::to_string(pid) + std::to_string(qid);
         std::string ring_name = "ring_tx_q" + std::to_string(pid) + std::to_string(qid);

         // 2. Create the Mempool
         mempools_[qid] = rte_pktmbuf_pool_create(
            mp_name.c_str(),
            8191,                      // Number of elements
            250,                       // Per-lcore cache size
            0,                         // Private data size
            RTE_MBUF_DEFAULT_BUF_SIZE, // Data buffer size
            rte_socket_id()            // NUMA socket
         );

         // 3. Create the Ring
         rings_[qid] = rte_ring_create(
            ring_name.c_str(),
            1024,                      // Must be power of 2
            rte_socket_id(),
            RING_F_SP_ENQ | RING_F_SC_DEQ // Single Producer/Consumer
         );
      }
   }
}

void PortHandlerLoopback::worker(VidContext ctx) {
   int vid = ctx.vid;
   std::vector<int> qids = ctx.qids;

   is_running_ = true;

   vtb::QueueStats* device_stats = config.get_stats_table()[vid];

   while (is_running_) {
      for (int qid : qids) {

        // Check if the device is still in the 'RUNNING' state
        // uint64_t flags;
        // if (rte_vhost_get_negotiated_features(vid, &flags) != 0) {
        //     VTB_LOG(WARNING) << "Device disconnected. Exiting worker loop.";
        //     is_running_ = false;
        //     break;
        // }
        
        // TODO CHECK this is an overhead. if still required, then check at
        // intervals and not for every pkt
        // to avoid VHOST_DATA: () rte_vhost_dequeue_burst: built-in vhost net backend is disabled
        // Check if the specific queue is actually enabled
        //
        // if (unlikely(!rte_vhost_vring_call(vid, qid))) {
        //     // If the vring is not ready/enabled, the VM is likely shutting down
        //     VTB_LOG(TRACE) << "PortHandlerLoopback: Ring not ready: " << qid;
        //     is_running_ = false;
        //     break;
        // }

         vtb::QueueStats& q_stat = device_stats[qid];

        if (qid % 2 != 0) {
           // It's a Transmit Queue (Odd)
           dequeue_tx_packets(vid, qid, mempools_[qid], rings_[qid], q_stat);
        } else {
           // It's a Receive Queue (Even)
           // It uses the ring from its associated TX partner (qid + 1)
           enqueue_rx_packets(vid, qid, rings_[qid + 1], q_stat);
        }
     }
     // Consider a tiny sched_yield() or small usleep if qids is small
     // to prevent 100% CPU spinning on empty queues
   }
}

void PortHandlerLoopback::dequeue_tx_packets(int vid, int qid, struct rte_mempool* mpool, struct rte_ring* ring, [[maybe_unused]] vtb::QueueStats& stats) {
   struct rte_mbuf* pkts[vtb::PKT_BURST_SZ];
   uint16_t nb_tx = rte_vhost_dequeue_burst(
         vid, 
         qid, 
         mpool, 
         pkts, 
         PKT_BURST_SZ);

   if (nb_tx == 0) {
      return;
   }
   
   for (uint16_t i=0; i<nb_tx; i++) {
      log_mbuf_hex(pkts[i], "TX_DEQUEUE:", vid, qid);
   }

	// Lossless enqueue into ring — retry until all are in
	uint16_t sent = 0;
	while (sent < nb_tx) {
	    unsigned int pushed = rte_ring_sp_enqueue_burst(
	        ring,
	        reinterpret_cast<void**>(&pkts[sent]),
	        nb_tx - sent,
	        nullptr);
	    sent += pushed;
	    if (sent < nb_tx)
	        usleep(1);  // backpressure: ring full, let consumer drain
	}
	// txq_pkt_cnt_ += sent;
   VTB_LOG(TRACE) << "PortHandlerLoopback: Dequeue" 
      << " VID: " << vid
      << " QID: " << qid
      << " Vhost Dequeued: " << nb_tx
      << " Ring Enqueued: " << sent;

   // CHECK why this fetch_add
   stats.tx_frames.fetch_add(sent, std::memory_order_relaxed);
   // stats.tx_frames.fetch_add(txq_pkt_cnt_, std::memory_order_relaxed);
}
 
void PortHandlerLoopback::enqueue_rx_packets(int vid, int qid, struct rte_ring* ring, [[maybe_unused]] vtb::QueueStats& stats) {
	struct rte_mbuf* pkts[PKT_BURST_SZ];
	unsigned int nb_deq = rte_ring_sc_dequeue_burst(
	    ring,
	    reinterpret_cast<void**>(pkts),
	    PKT_BURST_SZ,
	    nullptr);
	
	if (nb_deq == 0) {
	    return;
	}

	// Lossless enqueue to guest RX — retry until all accepted
	uint16_t sent = 0;
	uint32_t retries = 0;
   uint16_t nb_tx = 0;
	while (sent < nb_deq && retries < MAX_ENQUEUE_RETRIES) {
	    nb_tx = rte_vhost_enqueue_burst(vid, qid, &pkts[sent], nb_deq - sent);
	    sent += nb_tx;
	    if (sent < nb_deq) {
	        retries++;
	        usleep(1); // backpressure: guest RX ring full
	    }
	}

   for (uint16_t i=0; i<nb_tx; i++) {
      log_mbuf_hex(pkts[i], "RX_ENQUEUE:", vid, qid);
   }

	// rxq_pkt_cnt_ += sent;

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
	        ring, reinterpret_cast<void**>(pkts), PKT_BURST_SZ, nullptr);
	    for (unsigned int i = 0; i < remaining; i++)
	        rte_pktmbuf_free(pkts[i]);
	} while (remaining > 0);

   VTB_LOG(TRACE) << "PortHandlerLoopback: Enqueue" 
      << " VID: " << vid
      << " QID: " << qid
      << " Ring Dequeued: " << nb_deq
      << " Vhost Enqueued: " << sent;
   stats.rx_frames.fetch_add(sent, std::memory_order_relaxed);
   // stats.rx_frames.fetch_add(rxq_pkt_cnt_, std::memory_order_relaxed);
}

void PortHandlerLoopback::start([[maybe_unused]] int pid, int vid) {
   VTB_LOG(DEBUG) << "PortHandlerLoopback: Start called with" 
                  << " vid: "<< vid
                  << " pid: "<< pid;
   VidContext ctx = {};
   ctx.vid = vid;
   ctx.qids = get_queue_ids_by_vid(vid);

   create_resources(pid, ctx.qids);
   VTB_LOG(DEBUG) << "PortHandlerLoopback: Processing Queue ids: " << format_qids(ctx.qids);

   auto threading_mode = config.get_arg<std::string>("--threading-mode");
   dispatch(ctx, threading_mode);
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

   VTB_LOG(DEBUG) << "PortHandlerLoopback: Dispatching Queue ids: " << format_qids(qids);

   ThreadMode mode = string_to_thread_mode(mode_str);
   VTB_LOG(DEBUG) << "PortHandlerLoopback: Dispatch Threading mode: " << mode_str;

   switch (mode) {
      case ThreadMode::EachQTwoThread: // indepented threads
         for (int id=0; id<static_cast<int>(qids.size()); id++) {
            launch({vid, {id}});
         }
         break;

      case ThreadMode::EachQOneThread: // on thread per tx rx pair
         VTB_LOG(DEBUG) << "PortHandlerLoopback: EachQOneThread quid size: " <<qids.size();
         for (int id=0; id<static_cast<int>(qids.size()); id+=2) {
            VTB_LOG(DEBUG) << "PortHandlerLoopback: EachQOneThread:Launching";
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

      case ThreadMode::AllQOneThread: {
         launch({vid, qids});
         break;
      }

      default:
         // an undefine thread mode
         // TODO Add a fatal message and exit
         break;
   }
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
