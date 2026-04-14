#pragma once

#include <rte_ring.h>

#include "port_handler.h"

namespace vtb {

class PortHandlerLoopback : public PortHandler {
public:
    PortHandlerLoopback() = default;
    virtual ~PortHandlerLoopback() override;

    void start(int, int) override;
    void shutdown() override;
    void dispatch(const VidContext&, const std::string& mode);

protected:
    void worker(VidContext ctx) override;
    void launch(VidContext ctx) override;
    void create_resources(const int pid, const std::vector<int>& qids) override;

    void dequeue_tx_packets(int vid, int qid, struct rte_mempool*, struct rte_ring*);
    void enqueue_rx_packets(int vid, int qid, struct rte_ring*);

    void extract_tx_metadata() override;
    void decode_tx_metadata() override;
    void act_on_tx_metadata() override;
    void create_tx_port_metadata() override;
    void write_tx_packets() override;

    void read_rx_packets() override;
    void extract_rx_metadata() override;
    void decode_rx_metadata() override;
    void act_on_rx_metadata() override;
    void create_rx_vm_metadata() override;
};

} // namespace vtb
