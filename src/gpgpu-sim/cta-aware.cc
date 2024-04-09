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
 * Called by the scheduler to find the last warp whose prefetch request got serviced
 */
unsigned int CTA_Aware::CTA_Aware_Prefetcher::get_warp_id()
{
        // TODO: Verify correctness
        return this->last_serviced_warp_id;
}

/*
 * Called by the LDST unit with the uncoalesced addresses of all threads of a warp
 */
void CTA_Aware::CTA_Aware_Prefetcher::update_state(std::list<CTA_Aware::CTA_data_t> data)
{
        // TODO: Complete
        for(CTA_Aware::CTA_data_t& d: data)     // Executed once for each warp in the SM
        {
                // Create a set of coalesced addresses
                std::set<new_addr_type> unique_coalesced_adderesses;
                for(new_addr_type addr&: data.base_addresses)   // Executed once for each active thread in a warp
                {
                        new_addr_type masked_addr = addr & ~((1ULL << 8) - 1);
                        unique_coalesced_adderesses.insert(masked_addr); 
                }
        }
}

/*
 * Called by the LDST unit and returns the list of prefetch candidates
 */
std::list<new_addr_type> CTA_Aware::CTA_Aware_Prefetcher::generate_prefetch_candidates()
{
        // TODO
}
