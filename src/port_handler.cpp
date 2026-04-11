#include "port_handler.h"

namespace vtb {

void PortHandler::set_ids(int devid, int rqid, int tqid) {
   vid = devid;
   rxqid = rqid;
   txqid = tqid;
}

} // namespace vtb
