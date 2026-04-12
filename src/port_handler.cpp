#include "port_handler.h"

namespace vtb {

static vtb::ConfigManager& config = vtb::ConfigManager::get_instance();

std::vector<int> PortHandler::get_queue_ids_by_vid(int vid) {
   std::vector<int> result;

   // 1. Find the entry in pmap_ using the vid as a key - O(log N)
   auto it = config.get_pmap().find(vid);

   // 2. If the vid doesn't exist, return an empty vector (or throw an error)
   if (it == config.get_pmap().end()) {
      return result;
   }

   // 3. Get a reference to the PortMap to avoid copying the large arrays
   const PortMap& map_data = it->second;
   int count = map_data.vd.nof_queue_pairs;

   // Ensure we don't exceed the array bounds
   if (count > MAX_QUEUE_PAIRS) {
      count = MAX_QUEUE_PAIRS;
   }

   // 4. First, collect all txq_id values
   for (int i = 0; i < count; ++i) {
      result.push_back(map_data.vd.qp[i].txq_id);
   }

   // 5. Second, collect all rxq_id values
   for (int i = 0; i < count; ++i) {
      result.push_back(map_data.vd.qp[i].rxq_id);
   }

   return result;
}

} // namespace vtb
