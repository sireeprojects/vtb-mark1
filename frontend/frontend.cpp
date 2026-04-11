#include <rte_config.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_mbuf.h>
#include <rte_version.h>
#include <unistd.h>

#include <csignal>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

static constexpr uint32_t MBUF_POOL_SIZE = 8191;
static constexpr uint32_t MBUF_CACHE_SIZE = 256;
static constexpr uint16_t PKT_BURST_SZ = 32;
static constexpr uint16_t PRIV_DATA_SIZE = 48;
static constexpr uint16_t VIRTIO_RXQ = 0;
static constexpr uint16_t VIRTIO_TXQ = 1;
static constexpr uint32_t RING_SIZE = 4096;
static constexpr uint32_t MAX_ENQUEUE_RETRIES = 1000;

static volatile bool force_quit = false;
static void signal_handler(int signum) {
   (void)signum;
   std::cout << "[FRONTEND] *** <User Pressed Ctrl+C>" << std::endl;
   force_quit = true;
}

class CustomFrontend {
public:
   CustomFrontend(int argc, char** argv) {
      std::cout << "DPDK Version: " << rte_version() << std::endl;

      // Initialize EAL
      int ret = rte_eal_init(argc, argv);
      if (ret < 0) throw std::runtime_error("EAL Init Failed");

      uint16_t port_id;
      uint16_t nb_ports = rte_eth_dev_count_avail();

      // Create Mbuf Pool
      mbuf_pool_ = rte_pktmbuf_pool_create("FRONTEND_POOL", MBUF_POOL_SIZE,
         MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
      if (!mbuf_pool_) throw std::runtime_error("Mbuf Pool Allocation Failed");

      RTE_ETH_FOREACH_DEV(port_id) {
         struct rte_eth_dev_info dev_info;
         rte_eth_dev_info_get(port_id, &dev_info);

         // number of queues configure
         uint16_t nb_rx_q = dev_info.max_rx_queues;
         uint16_t nb_tx_q = dev_info.max_tx_queues;

         // printf("Initializing Port %u with %u RX and %u TX queues\n", port_id,
            // nb_rx_q, nb_tx_q);

         // Now use these values to configure the port
         struct rte_eth_conf port_conf = {0};
         rte_eth_dev_configure(port_id, nb_rx_q, nb_tx_q, &port_conf);

         for (int i = 0; i < nb_rx_q; i++) {
            if (rte_eth_rx_queue_setup(port_id, i, 1024,
                   rte_eth_dev_socket_id(port_id), NULL, mbuf_pool_) < 0)
               throw std::runtime_error("Failed to setup RX queue");
            if (rte_eth_tx_queue_setup(
                   port_id, i, 1024, rte_eth_dev_socket_id(port_id), NULL) < 0)
               throw std::runtime_error("Failed to setup TX queue");
         }
         if (rte_eth_dev_start(port_id) < 0)
            throw std::runtime_error("Failed to start port");

         std::cout << "[FRONTEND] " 
                   << "Port " << port_id 
                   << " Initialized and Started with "
                   << nb_rx_q << " queues"
                   << std::endl;
      }
   }

   void run() {
      // std::cout << ">>> Traffic loop active" << std::endl;
      uint16_t port_id = 0;

      while (!force_quit) {
         // send_burst(port_id);
         // receive_burst(port_id);
         // Throttle slightly to keep logs readable; remove for max performance
         rte_delay_us(500000);
      }

      RTE_ETH_FOREACH_DEV(port_id) {
         // std::cout << "Stopping Device: " << port_id << std::endl;
         rte_eth_dev_stop(port_id);
         // std::cout << "Closing Device: " << port_id << std::endl;
         rte_eth_dev_close(port_id);
         std::cout << "[FRONTEND] " 
                   << "Port " << port_id 
                   << " stopped and closed"
                   << std::endl;
      }
      sleep(1);
      rte_eal_cleanup();
   }

private:
   struct rte_mempool* mbuf_pool_;

   void send_burst(uint16_t port_id) {
      struct rte_mbuf* pkts[PKT_BURST_SZ];
      for (int i = 0; i < PKT_BURST_SZ; i++) {
         pkts[i] = rte_pktmbuf_alloc(mbuf_pool_);
         if (!pkts[i]) continue;

         uint8_t* ptr = rte_pktmbuf_mtod(pkts[i], uint8_t*);

         // Fill 48-byte metadata
         memset(ptr, 0xAA, PRIV_DATA_SIZE);

         // Ethernet Header starts after metadata
         struct rte_ether_hdr* eth =
            (struct rte_ether_hdr*)(ptr + PRIV_DATA_SIZE);
         rte_eth_random_addr(eth->src_addr.addr_bytes);
         rte_eth_random_addr(eth->dst_addr.addr_bytes);
         eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

         // Total length: 48 (meta) + 14 (eth) + 46 (payload) = 108 bytes
         pkts[i]->data_len = PRIV_DATA_SIZE + sizeof(struct rte_ether_hdr) + 46;
         pkts[i]->pkt_len = pkts[i]->data_len;
      }

      uint16_t sent = rte_eth_tx_burst(port_id, 0, pkts, PKT_BURST_SZ);
      if (sent > 0) {
         std::cout << "[FRONTEND] Sent " << sent << " packets." << std::endl;
      }

      // Free unsent packets
      if (unlikely(sent < PKT_BURST_SZ)) {
         for (uint16_t i = sent; i < PKT_BURST_SZ; i++)
            rte_pktmbuf_free(pkts[i]);
      }
   }

   void receive_burst(uint16_t port_id) {
      struct rte_mbuf* pkts[PKT_BURST_SZ];
      uint16_t rcved = rte_eth_rx_burst(port_id, 0, pkts, PKT_BURST_SZ);
      if (rcved > 0) {
         std::cout << "[FRONTEND] Received " << rcved
                   << " packets back from loopback." << std::endl;
         for (uint16_t i = 0; i < rcved; i++) rte_pktmbuf_free(pkts[i]);
      }
   }
};

int main(int argc, char** argv) {
   signal(SIGINT, signal_handler);
   try {
      CustomFrontend app(argc, argv);
      app.run();

   } catch (const std::exception& e) {
      std::cerr << "CRITICAL: " << e.what() << std::endl;
      return 1;
   }
   return 0;
}
