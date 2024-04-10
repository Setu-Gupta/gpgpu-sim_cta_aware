#include "cta-aware.h"

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
 * Coalesces addresses and returns a set
 */
std::vector<new_addr_type> CTA_Aware::CTA_Aware_Prefetcher::get_coalesced_addresses(std::map<unsigned int, new_addr_type>& addrs)
{
        // TODO: Verify correctness
        std::set<new_addr_type> coalesced_addresses;
        for(const std::pair<const unsigned int, new_addr_type>& p: addrs) // Executed once for each active thread in a warp
        {
                new_addr_type masked_addr = p.second & ~((1ULL << 8) - 1);
                coalesced_addresses.insert(masked_addr);
        }

        std::vector<new_addr_type> retvec;
        for(const new_addr_type& a: coalesced_addresses)
                retvec.push_back(a);
        return retvec;
}


bool CTA_Aware::CTA_Aware_Prefetcher::in_PerCTA(unsigned int CTA_ID, unsigned int PC)
{
        if(this->PerCTA_table.find(CTA_ID) == this->PerCTA_table.end())
                return false;
        return (PerCTA_table[CTA_ID].find(PC) != this->PerCTA_table[CTA_ID].end());
}

bool CTA_Aware::CTA_Aware_Prefetcher::in_Dist(unsigned int PC)
{
        return (Dist_table.find(PC) != this->Dist_table.end());
}

/*
 * Called by the LDST unit with the uncoalesced addresses of all threads of a warp
 */
std::list<new_addr_type> CTA_Aware::CTA_Aware_Prefetcher::generate_prefetch_candidates(std::list<CTA_Aware::CTA_data_t> data)
{
        std::list <new_addr_type> candidates;
        // TODO: Complete
        for(CTA_Aware::CTA_data_t& d: data) // Executed once for each warp in the SM
        {
                // Create a set of coalesced addresses
                std::vector<new_addr_type> coalesced_addresses = this->get_coalesced_addresses(d.base_addresses);
                if(coalesced_addresses.size() > VARIATION)
                {
                        std::cout << "Not tracking addresses for Warp " << d.Warp_ID
                                << " of CTA " << d.CTA_ID << ". Found "
                                <<  coalesced_addresses.size() << " addresses" << std::endl;
                        continue;
                }

                if(!this->in_PerCTA(d.CTA_ID, d.PC) && !this->in_Dist(d.PC))
                {
                        // This is the first time that we've seen this CTA, PC pair
                        this->PerCTA_table[d.CTA_ID].insert(std::make_pair(d.PC, PerCTA_entry_t(d.Warp_ID, std::move(coalesced_addresses))));
                }
                else if(!this->in_PerCTA(d.CTA_ID, d.PC) && this->in_Dist(d.PC))
                {
                        // This is the second instance of this PC indicating a repeated warp of the same CTA. Calculate stride
                        this->PerCTA_table[d.CTA_ID].insert(std::make_pair(d.PC, PerCTA_entry_t(d.Warp_ID, std::move(coalesced_addresses))));

                        // Compute the prefetch candidates
                        for(unsigned int idx = ((d.CTA_ID - 1)*this->num_warps_per_CTA); i < (d.CTA_ID * this->num_warps_per_CTA); i++)
                        {
                                long long int stride = this->Dist_table[d.PC];
                                int distance = idx - d.Warp_ID;
                                long long prefetch_candidate = stride * distance;
                                if(distance != 0)
                                        candidates.insert(prefetch_candidate);
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
                                for(std::pair<const unsigned int, std::map<unsigned int, PerCTA_entry_t>>& p: PerCTA_table)
                                {
                                        if(p.second.find(d.PC) == p.second.end())
                                        {
                                                for(unsigned int idx = ((d.CTA_ID - 1)*this->num_warps_per_CTA); i < (d.CTA_ID * this->num_warps_per_CTA); i++)
                                                {
                                                        long long int stride = this->Dist_table[d.PC];
                                                        int distance = idx - d.Warp_ID;
                                                        long long prefetch_candidate = stride * distance;
                                                        if(distance != 0)
                                                                candidates.insert(prefetch_candidate);
                                                }
                                        }
                                        continue;

                                }

                                for(unsigned int idx = ((d.CTA_ID - 1)*this->num_warps_per_CTA); i < (d.CTA_ID * this->num_warps_per_CTA); i++)
                                {
                                        long long int stride = this->Dist_table[d.PC];
                                        int distance = idx - d.Warp_ID;
                                        long long prefetch_candidate = stride * distance;
                                        if(distance != 0)
                                                candidates.insert(prefetch_candidate);
                                }
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
                }
        }
        return candidates;
}
