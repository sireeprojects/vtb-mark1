#include "port_handler.h"

#define BURST_SIZE 32
#define PRIV_DATA_SIZE 48
#define MBUF_CACHE_SIZE 256
#define MEMPOOL_SIZE 8191

#define RING_SIZE 2048
#define NUM_MBUFS 8191


namespace vtb {

PortHandler::PortHandler() : is_running_{false},
                             tx_mbuf_pool_{nullptr},
                             rx_mbuf_pool_{nullptr},
                             tx_ring_{nullptr},
                             rx_ring_{nullptr}
{
}

PortHandler::~PortHandler() {
//   stop();
//   if (tx_thread_.joinable())
//      tx_thread_.join();
//   if (rx_thread_.joinable())
//      rx_thread_.join();
//   if (tx_rx_thread_.joinable())
//      tx_rx_thread_.join();
}

void PortHandler::stop() {
   VTB_LOG(DEBUG) << "PortHandlerLoopback: Stopped called for"
               << " VID: " << vid
               << " RXQ: " << rxqid
               << " TXQ: " << txqid;
   is_running_ = false;
}

} // namespace vtb
