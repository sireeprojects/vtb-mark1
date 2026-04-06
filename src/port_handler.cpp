#include "port_handler.h"

namespace vtb {

void PortHandler::stop() {
   VTB_LOG(DEBUG) << "PortHandlerLoopback: Stopped called for"
               << " VID: " << vid
               << " RXQ: " << rxqid
               << " TXQ: " << txqid;
   is_running_ = false;
}

void PortHandler::set_ids(int devid, int rqid, int tqid) {
   vid = devid;
   rxqid = rqid;
   txqid = tqid;
}

} // namespace vtb
