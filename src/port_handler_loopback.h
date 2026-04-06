#pragma once

#include "port_handler.h"

#include <rte_ring.h>

namespace vtb {

/**
 * @brief Loopback implementation of the PortHandler interface.
 * Inherits all protected pipeline and worker methods.
 */
class PortHandlerLoopback : public PortHandler {
public:
    PortHandlerLoopback();
    virtual ~PortHandlerLoopback() override;

    void start() override;

protected:
    // --- Worker Overrides (Optional, but included as empty per request) ---
    void tx_worker() override;
    void rx_worker() override;
    void tx_rx_worker() override;

    // --- TX Pipeline Overrides ---
    void dequeue_tx_packets() override;
    void extract_tx_metadata() override;
    void decode_tx_metadata() override;
    void act_on_tx_metadata() override;
    void create_tx_port_metadata() override;
    void write_tx_packets() override;

    // --- RX Pipeline Overrides ---
    void read_rx_packets() override;
    void extract_rx_metadata() override;
    void decode_rx_metadata() override;
    void act_on_rx_metadata() override;
    void create_rx_vm_metadata() override;
    void enqueue_rx_packets() override;
};

} // namespace vtb
