#pragma once

#include <rte_ring.h>
// #include <vector>

#include "port_handler.h"

namespace vtb {

class PortHandlerLoopback : public PortHandler {
public:
    PortHandlerLoopback() = default;
    virtual ~PortHandlerLoopback() override;

    void start(int, int) override;
    void shutdown() override;

    ThreadMode string_to_thread_mode(std::string_view mode_str);
    void dispatch(const VidContext&, const std::string& mode);

protected:
    // void txq_worker() override;
    // void rxq_worker() override;
    // void txq_rxq_worker() override;
    void worker(VidContext ctx) override;
    void launch(VidContext ctx) override;

    void create_resources(const int pid, const std::vector<int>& qids);

    std::map<int, struct rte_mempool*> mempools_;
    std::map<int, struct rte_ring*> rings_;

    void dequeue_tx_packets(int vid, int qid, struct rte_mempool*, struct rte_ring*);
    void enqueue_rx_packets(int vid, int qid, struct rte_ring*);

    void dequeue_tx_packets() override;
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
    void enqueue_rx_packets() override;
};

} // namespace vtb
