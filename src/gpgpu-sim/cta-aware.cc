#include "cta-aware.h"

/*
 * Coalesces addresses and returns a set
 */
std::vector<new_addr_type> CTA_Aware::CTA_Aware_Prefetcher::get_coalesced_addresses(const std::vector<new_addr_type>& addrs) const
{
        // TODO: Verify correctness
        std::set<new_addr_type> coalesced_addresses;
        for(const new_addr_type& a: addrs) // Executed once for each active thread in a warp
        {
                new_addr_type masked_addr = a & ~((1ULL << 8) - 1);
                coalesced_addresses.insert(masked_addr);
        }

        std::vector<new_addr_type> retvec;
        for(const new_addr_type& a: coalesced_addresses)
                retvec.push_back(a);
        return retvec;
}

/*
 * Checkes whether there is an entry mapped to the given CTA_ID and PC in the PerCTA Table
 */
bool CTA_Aware::CTA_Aware_Prefetcher::in_PerCTA(const unsigned int CTA_ID, const unsigned int PC) const
{
        // TODO: Verify correctness
        auto cta_iter = this->PerCTA_table.find(CTA_ID);
        if(cta_iter == this->PerCTA_table.end())
                return false;
        
        auto& pc_map = cta_iter->second;
        return (pc_map.find(PC) != pc_map.end());
}

/*
 * Checkes whether there is an entry mapped to the given PC in the Dist Table
 */
bool CTA_Aware::CTA_Aware_Prefetcher::in_Dist(unsigned int PC) const
{
        // TODO: Verify correctness
        return (Dist_table.find(PC) != this->Dist_table.end());
}

/*
 * Returns the size of the PerCTA Table
 */
std::size_t CTA_Aware::CTA_Aware_Prefetcher::size_of_PerCTA(unsigned int CTA_ID) const
{
        // TODO: Verify correctness
        //std::size_t total_enteries = 0;
        
        // for(const std::pair<const unsigned int, std::map<unsigned int, PerCTA_entry_t>>& entry: this->PerCTA_table)
        //         total_enteries += entry.second.size();
        auto entry = PerCTA_table.find(CTA_ID);
        return entry->second.size();
}

/*
 * Returns the size of the Dist Table
 */
std::size_t CTA_Aware::CTA_Aware_Prefetcher::size_of_Dist() const
{
        // TODO: Verify correctness
        return this->Dist_table.size();
}

/*
 * Inserts an entry at CTA_ID, PC in the PerCTA Table. Evicts the least recently used entry if the table size exceeeds MAX_CTA_TABLE_SIZE after insertion
 */
void CTA_Aware::CTA_Aware_Prefetcher::insert_in_PerCTA(unsigned int CTA_ID, unsigned int PC, CTA_Aware::PerCTA_entry_t&& entry)
{
       // TODO: Complete
       PerCTA_table[CTA_ID][PC] = entry;
       if(size_of_PerCTA(CTA_ID) > MAX_CTA_TABLE_SIZE)
       {
                auto entry = PerCTA_table.find(CTA_ID);
                long long lru = std::numeric_limits<long long>::max();
                unsigned evicted = 0;
                for(auto it = entry->second.begin(); it != entry->second.end(); it++)
                {
                        if(it->second.cycle < lru)
                        {
                                lru = it->second.cycle;
                                evicted = it->first;
                        }
                }
                assert(evicted != 0);
                PerCTA_table[CTA_ID].erase(evicted);
       }
}

/*
 * Inserts an entry at CTA_ID, PC in the Dist Table. Evicts the least recently used entry if the table size exceeeds MAX_DIST_TABLE_SIZE after insertion
 */
void CTA_Aware::CTA_Aware_Prefetcher::insert_in_Dist(unsigned int PC, CTA_Aware::Dist_entry_t&& entry)
{
       // TODO: Complete 
       Dist_table[PC] = entry;
       if(size_of_Dist() > MAX_DIST_TABLE_SIZE)
       {
                long long lru = std::numeric_limits<long long>::max();
                unsigned evicted = 0;
                for(auto it = Dist_table.begin(); it != Dist_table.end(); it++)
                {
                        if(it->second.cycle < lru)
                        {
                                lru = it->second.cycle;
                                evicted = it->first;
                        }
                }
                assert(evicted != 0);
                Dist_table.erase(evicted);
       }
}

/*
 * Called by LDST unit whenever a prefetch request gets serviced
 * */
void CTA_Aware::CTA_Aware_Prefetcher::mark_request_serviced(unsigned int warp_id)
{
        // TODO: Verify correctness
        this->last_serviced_warp_id = warp_id;
}

/*
 * Called by the scheduler to find the last warp whose prefetch request got serviced. Resets the last_serviced_warp before returning
 */
unsigned int CTA_Aware::CTA_Aware_Prefetcher::get_warp_id()
{
        // TODO: Verify correctness
        unsigned int retval         = this->last_serviced_warp_id;
        this->last_serviced_warp_id = this->INVALID;
        return retval;
}

/*
 * Called by the LDST unit with the uncoalesced addresses of all threads of a warp
 */
std::list<new_addr_type> CTA_Aware::CTA_Aware_Prefetcher::generate_prefetch_candidates(std::list<CTA_Aware::CTA_data_t> data, unsigned long long cycle)
{
        std::list <new_addr_type> candidates;
        // TODO: Complete
        for(CTA_Aware::CTA_data_t& d: data) // Executed once for each warp in the SM
        {
                // Create a set of coalesced addresses
                std::vector<new_addr_type> coalesced_addresses = this->get_coalesced_addresses(d.base_addresses);
                if(coalesced_addresses.size() > VARIATION)
                {
                        // std::cout << "Not tracking addresses for Warp " << d.Warp_ID
                        //         << " of CTA " << d.CTA_ID << ". Found "
                        //         <<  coalesced_addresses.size() << " addresses" << std::endl;
                        continue;
                }

                if(!this->in_PerCTA(d.CTA_ID, d.PC) && !this->in_Dist(d.PC))
                {
                        // This is the first time that we've seen this CTA, PC pair
                        this->PerCTA_table[d.CTA_ID].insert(std::make_pair(d.PC, PerCTA_entry_t(d.Warp_ID, std::move(coalesced_addresses))));
                        this->print_PerCTA_table();
                }
                else if(!this->in_PerCTA(d.CTA_ID, d.PC) && this->in_Dist(d.PC))
                {
                        // This is the second instance of this PC indicating a repeated warp of the same CTA. Calculate stride
                        this->PerCTA_table[d.CTA_ID].insert(std::make_pair(d.PC, PerCTA_entry_t(d.Warp_ID, std::move(coalesced_addresses))));

                        // Compute the prefetch candidates
                        for(unsigned int idx = ((d.CTA_ID - 1) * d.num_warps); idx < (d.CTA_ID * d.num_warps); idx++)
                        {
                                long long int stride = this->Dist_table[d.PC].stride;
                                int distance = idx - d.Warp_ID;
                                long long prefetch_candidate = stride * distance;
                                if(distance != 0)
                                        candidates.emplace_back(prefetch_candidate);
                        }
                }
                else if(this->in_PerCTA(d.CTA_ID, d.PC) && !this->in_Dist(d.PC))
                {
                        // Calculate the stride
                        PerCTA_entry_t prev_entry = this->PerCTA_table[d.CTA_ID][d.PC];

                        // Sort the addresses and find the smaller vector
                        std::sort(prev_entry.base_addresses.begin(), prev_entry.base_addresses.end());
                        std::sort(coalesced_addresses.begin(), coalesced_addresses.end());

                        std::size_t num_addr = std::min(prev_entry.base_addresses.size(), coalesced_addresses.size());
                        if(num_addr == 0)
                                continue;

                        std::set<long long int> strides;
                        for(std::size_t i = 0; i < num_addr; i++)
                        {
                                long long int stride = coalesced_addresses.at(i) - prev_entry.base_addresses.at(i);
                                if(stride != 0)
                                        strides.insert(stride);
                        }

                        // Only update the Dist table and generate prefetch candidates if there is a single stride
                        if(strides.size() == 1)
                        {
                                // TODO: Complete
                                this->Dist_table.insert(std::make_pair(d.PC, Dist_entry_t(*strides.begin())));

                                // Compute the prefetch candidates
                                long long int stride = this->Dist_table[d.PC].stride;
                                for(std::pair<const unsigned int, std::map<unsigned int, PerCTA_entry_t>>& p: PerCTA_table)
                                {
                                        if(p.first == d.CTA_ID)
                                        {
                                                for(unsigned int idx = ((d.CTA_ID - 1)*d.num_warps); idx < (d.CTA_ID * d.num_warps); idx++)
                                                {
                                                        int distance = idx - d.Warp_ID;
                                                        new_addr_type prefetch_candidate = stride * distance;
                                                        if(distance != 0)
                                                                candidates.emplace_back(prefetch_candidate);
                                                }
                                        }
                                        else
                                        {
                                                auto it = p.second.find(d.PC);
                                                if(it != p.second.end())
                                                {
                                                        for(unsigned int idx = ((d.CTA_ID - 1)*d.num_warps); idx < (d.CTA_ID * d.num_warps); idx++)
                                                        {
                                                                int distance = idx - it->second.leading_warp_id;
                                                                if(distance != 0)
                                                                {
                                                                        for(new_addr_type base : it->second.base_addresses)
                                                                        {
                                                                                new_addr_type prefetch_candidate = base + (stride * distance);
                                                                                candidates.emplace_back(prefetch_candidate);   
                                                                        }   
                                                                }
                                                        }   
                                                }
                                                
                                        }
                                        continue;

                                }

                                // for(unsigned int idx = ((d.CTA_ID - 1)*this->num_warps_per_CTA); idx < (d.CTA_ID * this->num_warps_per_CTA); idx++)
                                // {
                                //         long long int stride = this->Dist_table[d.PC].stride;
                                //         int distance = idx - d.Warp_ID;
                                //         long long prefetch_candidate = stride * distance;
                                //         if(distance != 0)
                                //                 candidates.insert(prefetch_candidate);
                                // }
                        }

                }
                else if(this->in_PerCTA(d.CTA_ID, d.PC) && this->in_Dist(d.PC))
                {
                        // Calculate the stride
                        PerCTA_entry_t prev_entry = this->PerCTA_table[d.CTA_ID][d.PC];

                        // Sort the addresses and find the smaller vector
                        std::sort(prev_entry.base_addresses.begin(), prev_entry.base_addresses.end());
                        std::sort(coalesced_addresses.begin(), coalesced_addresses.end());

                        std::size_t num_addr = std::min(prev_entry.base_addresses.size(), coalesced_addresses.size());
                        if(num_addr == 0)
                                continue;

                        std::set<long long int> strides;
                        for(std::size_t i = 0; i < num_addr; i++)
                        {
                                long long int stride = coalesced_addresses.at(i) - prev_entry.base_addresses.at(i);
                                if(stride != 0)
                                        strides.insert(stride);
                        }
                        
                        if(strides.size() > 1 || strides.size() == 0)
                                this->Dist_table[d.PC].misprediction_counter++;
                        else if(*strides.begin() != this->Dist_table[d.PC].stride)
                                this->Dist_table[d.PC].misprediction_counter++;
                        
                        // TODO: Complete
                        if(d.Warp_ID == prev_entry.leading_warp_id)
                        {
                                if(std::find(prev_entry.base_addresses.begin(), prev_entry.base_addresses.end(), d.base_addresses[0]) == prev_entry.base_addresses.end())
                                {
                                        if(this->Dist_table[d.PC].misprediction_counter < 128)
                                        {
                                                prev_entry.base_addresses = d.base_addresses;
                                                long long int stride = this->Dist_table[d.PC].stride;
                                                for(unsigned int idx = ((d.CTA_ID - 1)*d.num_warps); idx < (d.CTA_ID * d.num_warps); idx++)
                                                {
                                                        int distance = idx - d.Warp_ID;
                                                        long long prefetch_candidate = stride * distance;
                                                        if(distance != 0)
                                                        {
                                                                for(new_addr_type base : prev_entry.base_addresses)
                                                                {
                                                                        new_addr_type prefetch_candidate = base + (stride * distance);
                                                                        candidates.emplace_back(prefetch_candidate);   
                                                                }
                                                        }
                                                }
                                        }
                                }
                        }
                }
        }
        return candidates;
}

void CTA_Aware::CTA_Aware_Prefetcher::print_PerCTA_table()
{
          for (const auto& outer_pair : this->PerCTA_table) {
                  std::cout << "CTA_ID: " << outer_pair.first << std::endl;
                  for (const auto& inner_pair : outer_pair.second) {
                          std::cout << "  PC: " << inner_pair.first << " Warp_ID: " << inner_pair.second.leading_warp_id << " Cycle: " << inner_pair.second.cycle << std::endl;
                          std::cout << "  Address: ";
                          for(const auto& base : inner_pair.second.base_addresses)
                          {
                                  std::cout << base << " ";
                          }
                          std::cout << std::endl;
                  }
          }
}

void CTA_Aware::CTA_Aware_Prefetcher::print_Dist_table()
{
        for (const auto& outer_pair : this->Dist_table) {
                std::cout << "PC: " << outer_pair.first << std::endl;
                std::cout << "Delta: " << outer_pair.second.stride << "m_counter: " << outer_pair.second.misprediction_counter << std::endl;
       }
}
