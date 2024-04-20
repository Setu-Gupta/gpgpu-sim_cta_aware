#include "cta-aware.h"


#define REMOVE_WHEN_misprediction_counter_IS_128 false

/*
 * Coalesces addresses and returns a set
 */
std::vector<new_addr_type> CTA_Aware::CTA_Aware_Prefetcher::get_coalesced_addresses(const std::vector<new_addr_type>&& addrs) const
{
        std::set<new_addr_type> coalesced_addresses;
        for(const new_addr_type& a: addrs) // Executed once for each active thread in a warp
        {
                new_addr_type masked_addr = a & ~((1ULL << COALESCING_BITS) - 1);
                coalesced_addresses.insert(masked_addr);
        }

        std::vector<new_addr_type> retvec;
        for(const new_addr_type& a: coalesced_addresses) 
        {
                retvec.push_back(a);
        }
        return retvec;
}

new_addr_type CTA_Aware::CTA_Aware_Prefetcher::get_coalesced_addresses(const new_addr_type addrs) const
{
        return addrs & ~((1ULL << COALESCING_BITS) - 1);
}

/*
 * Checks whether there is an entry mapped to the given CTA_ID and PC in the PerCTA Table
 */
bool CTA_Aware::CTA_Aware_Prefetcher::in_PerCTA(const unsigned int CTA_ID, const unsigned int PC) const
{
        auto cta_iter = this->PerCTA_table.find(CTA_ID);
        if(cta_iter == this->PerCTA_table.end())
                return false;

        auto& pc_map = cta_iter->second;
        return (pc_map.find(PC) != pc_map.end());
}

/*
 * Returns the number of enteries for the given CTA in the PerCTA Table
 */
std::size_t CTA_Aware::CTA_Aware_Prefetcher::size_of_PerCTA(unsigned int CTA_ID) const
{
        auto entry = PerCTA_table.find(CTA_ID);
        if(entry == PerCTA_table.end())
                return 0;
        return entry->second.size();
}

/*
 * Inserts an entry at CTA_ID, PC in the PerCTA Table. Evicts the least recently used entry if the table size exceeds MAX_CTA_TABLE_SIZE after insertion
 */
void CTA_Aware::CTA_Aware_Prefetcher::insert_in_PerCTA(unsigned int CTA_ID, unsigned int PC, CTA_Aware::PerCTA_entry_t&& entry)
{
        PerCTA_table[CTA_ID][PC] = entry;
        if(size_of_PerCTA(CTA_ID) > MAX_CTA_TABLE_SIZE)
        {
                auto               entry   = PerCTA_table.find(CTA_ID);
                unsigned long long lru     = std::numeric_limits<unsigned long long>::max();
                unsigned int       evicted = 0;
                for(auto it = entry->second.begin(); it != entry->second.end(); it++)
                {
                        if(it->second.cycle < lru)
                        {
                                lru     = it->second.cycle;
                                evicted = it->first;
                        }
                }
                assert(evicted != 0);
                PerCTA_table[CTA_ID].erase(evicted);
        }
}

/*
 * Prints the contents of the PerCTA table. This is used for debugging purposes
 */
void CTA_Aware::CTA_Aware_Prefetcher::print_PerCTA_table() const
{
        for(const auto& outer_pair: this->PerCTA_table)
        {
                std::cout << "Shader_ID: " << shader_id << "  CTA_ID: " << outer_pair.first ;
                for(const auto& inner_pair: outer_pair.second)
                {
                        std::cout << "  PC: " << inner_pair.first << " Warp_ID: " << inner_pair.second.leading_warp_id << " Cycle: " << inner_pair.second.cycle ;
                        std::cout << "  Address: ";
                        for(const auto& base: inner_pair.second.base_addresses)
                                std::cout << base << " ";
                        std::cout << std::endl;
                }
        }
}

/*
 * Checks whether there is an entry mapped to the given PC in the Dist Table
 */
bool CTA_Aware::CTA_Aware_Prefetcher::in_Dist(unsigned int PC) const
{
        return (Dist_table.find(PC) != this->Dist_table.end());
}

/*
 * Returns the size of the Dist Table
 */
std::size_t CTA_Aware::CTA_Aware_Prefetcher::size_of_Dist() const
{
        return this->Dist_table.size();
}

/*
 * Inserts an entry at CTA_ID, PC in the Dist Table. Evicts the least recently used entry if the table size exceeds MAX_DIST_TABLE_SIZE after insertion
 */
void CTA_Aware::CTA_Aware_Prefetcher::insert_in_Dist(unsigned int PC, CTA_Aware::Dist_entry_t&& entry)
{
        Dist_table[PC] = entry;
        if(size_of_Dist() > MAX_DIST_TABLE_SIZE)
        {
                unsigned long long lru     = std::numeric_limits< unsigned long long>::max();
                unsigned           evicted = 0;
                for(auto it = Dist_table.begin(); it != Dist_table.end(); it++)
                {
                        if(it->second.misprediction_counter > MISPRED_THRESH){
                                lru = 0;
                                evicted = it->first;
                        }
                        if(it->second.cycle < lru)
                        {
                                lru     = it->second.cycle;
                                evicted = it->first;
                        }
                }
                assert(evicted != 0);
                Dist_table.erase(evicted);
        }
}

/*
 * Prints the contents of the Dist table
 * Used for debugging purposes
 */
void CTA_Aware::CTA_Aware_Prefetcher::print_Dist_table() const
{
        for(const auto& outer_pair: this->Dist_table)
        {
                std::cout << "Shader_ID: " << shader_id << "  PC: " << outer_pair.first ;
                std::cout << "  Delta: " << outer_pair.second.stride << " m_counter: " << outer_pair.second.misprediction_counter << " c_counter: " << outer_pair.second.correct_counter;
                if(outer_pair.second.misprediction_counter)
                        std::cout << " expected Delta " << outer_pair.second.expected_stride;
                std::cout << std::endl;
        }
}

/*
 * Called by LDST unit whenever a prefetch request gets serviced
 * */
void CTA_Aware::CTA_Aware_Prefetcher::mark_request_serviced(unsigned int warp_id)
{
        this->last_serviced_warp_id = warp_id;
}

/*
 * Called by the scheduler to find the last warp whose prefetch request got serviced. Resets the last_serviced_warp before returning
 */
unsigned int CTA_Aware::CTA_Aware_Prefetcher::get_warp_id()
{
        unsigned int retval         = this->last_serviced_warp_id;
        this->last_serviced_warp_id = this->INVALID;
        return retval;
}

/*
 * Called by the LDST unit with the uncoalesced addresses of all threads of a warp
 */
void CTA_Aware::CTA_Aware_Prefetcher::generate_prefetch_candidates(std::list<CTA_Aware::CTA_data_t> data, unsigned long long cycle)
{ 
        std::list<std::pair<new_addr_type, unsigned int>> candidates;
        bool flag = 0; // Flag == 1 indicates that this is a new execution for that warp
        for(CTA_Aware::CTA_data_t& d: data) // Executed once for each warp in the SM
        {
                if(warp_base_addr.find(d.Warp_ID) != warp_base_addr.end())
                {
                        new_addr_type base_addr = warp_base_addr[d.Warp_ID];
                        if(base_addr != d.base_addresses.front())
                        {
                                warp_base_addr[d.Warp_ID] = d.base_addresses.front();
                                flag = 1;
                        }
                }
                else
                {
                        warp_base_addr[d.Warp_ID] = d.base_addresses.front();
                        flag = 1;
                }
                if(flag)
                {
                        // Create a set of coalesced addresses
                        std::vector<new_addr_type> coalesced_addresses = this->get_coalesced_addresses(std::move(d.base_addresses));
                        if(coalesced_addresses.size() > VARIATION)
                                continue;

                        if(!this->in_PerCTA(d.CTA_ID, d.PC) && !this->in_Dist(d.PC))
                        {
                                // This is the first time that we've seen this CTA, PC pair
                                this->PerCTA_table[d.CTA_ID].insert(std::make_pair(d.PC, PerCTA_entry_t(d.Warp_ID, std::move(d.base_addresses), cycle)));
                                this->print_PerCTA_table();
                        }
                        else if(!this->in_PerCTA(d.CTA_ID, d.PC) && this->in_Dist(d.PC))
                        {
                                // This is the second instance of this PC indicating a repeated warp of the same CTA. Calculate stride
                                this->PerCTA_table[d.CTA_ID].insert(std::make_pair(d.PC, PerCTA_entry_t(d.Warp_ID, std::move(d.base_addresses), cycle)));
                                this->print_PerCTA_table();
                                // Compute the prefetch candidates
                                long long int stride = this->Dist_table[d.PC].stride;
                                if(this->Dist_table[d.PC].misprediction_counter < 128) {
                                        for(unsigned int idx = (d.CTA_ID * d.num_warps); idx < ((d.CTA_ID + 1) * d.num_warps); idx++)
                                        {
                                                int distance = idx - d.Warp_ID;
                                                if(distance != 0)
                                                {
                                                        std::vector<new_addr_type> addrs;
                                                        for(new_addr_type base: this->PerCTA_table[d.CTA_ID][d.PC].base_addresses)
                                                        {
                                                                new_addr_type prefetch_candidate = base + (stride * distance);
                                                                addrs.push_back(prefetch_candidate);   
                                                        }
                                                        std::vector<new_addr_type> coalesced_addresses = this->get_coalesced_addresses(std::move(addrs));
                                                        for(new_addr_type& a : coalesced_addresses)
                                                        {
                                                                std::cout << "Shader_ID: " << shader_id << "  CTA_ID: " << d.CTA_ID << "  Warp_ID: " << idx << " PC: " << d.PC << " Prefetching address: " << a << " Stride " << stride << " Distance " << distance << " C1" << std::endl;
                                                                candidates.push_back(std::make_pair(a, idx));
                                                        }   
                                                }
                                        }
                                }
                        }
                        else if(this->in_PerCTA(d.CTA_ID, d.PC) && !this->in_Dist(d.PC))
                        {
                                // We have seen this CTA and this PC before but we haven't calculated the stride yet

                                PerCTA_entry_t prev_entry = this->PerCTA_table[d.CTA_ID][d.PC];
                                this->PerCTA_table[d.CTA_ID][d.PC].cycle = cycle;

                                // Sort the addresses and find the smaller vector
                                std::sort(prev_entry.base_addresses.begin(), prev_entry.base_addresses.end());
                                std::sort(d.base_addresses.begin(), d.base_addresses.end());

                                std::size_t num_addr = std::min(prev_entry.base_addresses.size(), d.base_addresses.size());
                                if(num_addr == 0)
                                        continue;

                                int difference = d.Warp_ID - prev_entry.leading_warp_id;
                                std::set<long long int> strides;
                                for(std::size_t i = 0; i < num_addr; i++)
                                {
                                        long long int stride = d.base_addresses.at(i) - prev_entry.base_addresses.at(i);
                                        if(stride != 0 && difference != 0)
                                                strides.insert(stride/difference);
                                }

                                // Only update the Dist table and generate prefetch candidates if there is a single stride
                                if(strides.size() == 1)
                                {       
                                        assert(this->Dist_table.find(d.PC) == this->Dist_table.end() && "Dist table should not have an entry for this PC");
                                        this->Dist_table.insert(std::make_pair(d.PC, Dist_entry_t(*strides.begin(), cycle)));
                                        this->print_Dist_table();
                                        // Compute the prefetch candidates
                                        long long int stride = this->Dist_table[d.PC].stride;

                                        // At this point we haven't issued prefetch requests for other CTAs as we don't have the stride. Therefore, issue for all CTAs
                                        for(const auto& p: PerCTA_table)
                                        {
                                                if(p.first == d.CTA_ID) // Prefetching for the remaining warps of the same CTA
                                                {
                                                        // for(unsigned int idx = (d.CTA_ID * d.num_warps); idx < ((d.CTA_ID + 1) * d.num_warps); idx++)
                                                        // {
                                                        //         int distance = idx - d.Warp_ID;
                                                        //         if(distance != 0)
                                                        //         {
                                                        //                 for(new_addr_type base: this->PerCTA_table[d.CTA_ID][d.PC].base_addresses)
                                                        //                 {
                                                        //                         new_addr_type prefetch_candidate = base + (stride * distance);
                                                        //                         std::cout << "Shader_ID: " << shader_id << "  CTA_ID: " << d.CTA_ID << "  Warp_ID: " << idx << " PC: " << d.PC << " Prefetching address: " << prefetch_candidate << " Stride " << stride << " Distance " << distance << " C2"<< std::endl;
                                                        //                         candidates.push_back(std::make_pair(prefetch_candidate, idx));
                                                        //                 }
                                                        //         }
                                                        // }
                                                        continue;
                                                }
                                                else // Prefetching for other CTAs
                                                {
                                                        auto it = p.second.find(d.PC);
                                                        if(it != p.second.end())
                                                        {
                                                                for(unsigned int idx = ((p.first - 1) * d.num_warps); idx < (p.first * d.num_warps); idx++)
                                                                {
                                                                        int distance = idx - d.Warp_ID;
                                                                        if(distance != 0)
                                                                        {
                                                                                std::vector<new_addr_type> addrs;
                                                                                for(new_addr_type base: it->second.base_addresses)
                                                                                {
                                                                                        new_addr_type prefetch_candidate = base + (stride * distance);
                                                                                        addrs.push_back(prefetch_candidate);   
                                                                                }
                                                                                std::vector<new_addr_type> coalesced_addresses = this->get_coalesced_addresses(std::move(addrs));
                                                                                for(new_addr_type& a : coalesced_addresses)
                                                                                {
                                                                                        std::cout << "Shader_ID: " << shader_id << "  CTA_ID: " << d.CTA_ID << "  Warp_ID: " << idx << " PC: " << d.PC << " Prefetching address: " << a << " Stride " << stride << " Distance " << distance << " C1" << std::endl;
                                                                                        candidates.push_back(std::make_pair(a, idx));
                                                                                }  
                                                                        }
                                                                }
                                                        }
                                                }
                                        }
                                }
                        }
                        else if(this->in_PerCTA(d.CTA_ID, d.PC) && this->in_Dist(d.PC))
                        {
                                // Calculate the stride
                                PerCTA_entry_t prev_entry = this->PerCTA_table[d.CTA_ID][d.PC];
                                this->PerCTA_table[d.CTA_ID][d.PC].cycle = cycle;

                                // Sort the addresses and find the smaller vector
                                std::sort(prev_entry.base_addresses.begin(), prev_entry.base_addresses.end());
                                std::sort(d.base_addresses.begin(), d.base_addresses.end());

                                std::size_t num_addr = std::min(prev_entry.base_addresses.size(), d.base_addresses.size());
                                if(num_addr == 0)
                                        continue;

                                int difference = d.Warp_ID - prev_entry.leading_warp_id;
                                std::set<long long int> strides;
                                for(std::size_t i = 0; i < num_addr; i++)
                                {
                                        long long int stride = d.base_addresses.at(i) - prev_entry.base_addresses.at(i);
                                        if(stride != 0 && difference != 0)
                                                strides.insert(stride/difference);
                                }
                                this->print_Dist_table();
                                if(strides.size() > 1 || strides.size() == 0)
                                        // this->Dist_table[d.PC].misprediction_counter = 128;
                                        this->Dist_table[d.PC].misprediction_counter++;
                                else if(*strides.begin() != this->Dist_table[d.PC].stride){
                                        //this->Dist_table[d.PC].misprediction_counter = 128;
                                        this->Dist_table[d.PC].expected_stride = *strides.begin();
                                        this->Dist_table[d.PC].misprediction_counter++;
                                        this->print_Dist_table();
                                        std::cout << "Shader_ID: " << shader_id << " Saba Removing entry from Dist table for PC: " << d.PC << " delta : " << this->Dist_table[d.PC].stride;
                                        this->Dist_table.erase(d.PC);
                                } else if(*strides.begin() == this->Dist_table[d.PC].stride)
                                        this->Dist_table[d.PC].correct_counter++;


                                if(d.Warp_ID == prev_entry.leading_warp_id) // This is the second iteration of the same PC from the same CTA
                                {
                                        if(prev_entry.base_addresses != d.base_addresses) // New set of base address found
                                        {
                                                // Update the PerCTA entry to reflect new base addresses
                                                this->PerCTA_table[d.CTA_ID][d.PC].base_addresses = d.base_addresses;
                                                this->PerCTA_table[d.CTA_ID][d.PC].cycle = cycle;

                                                if(this->Dist_table[d.PC].misprediction_counter < 128)
                                                {
                                                        // Compute the prefetch candidates
                                                        long long int stride = this->Dist_table[d.PC].stride;

                                                        for(const auto& p: PerCTA_table)
                                                        {
                                                                if(p.first == d.CTA_ID) // Prefetching for the remaining warps of the same CTA
                                                                {
                                                                        for(unsigned int idx = (d.CTA_ID * d.num_warps); idx < ((d.CTA_ID + 1) * d.num_warps); idx++)
                                                                        {
                                                                                int distance = idx - d.Warp_ID;
                                                                                if(distance != 0)
                                                                                {
                                                                                        std::vector<new_addr_type> addrs;
                                                                                        for(new_addr_type base: this->PerCTA_table[d.CTA_ID][d.PC].base_addresses)
                                                                                        {
                                                                                                new_addr_type prefetch_candidate = base + (stride * distance);
                                                                                                addrs.push_back(prefetch_candidate);   
                                                                                        }
                                                                                        std::vector<new_addr_type> coalesced_addresses = this->get_coalesced_addresses(std::move(addrs));
                                                                                        for(new_addr_type& a : coalesced_addresses)
                                                                                        {
                                                                                                std::cout << "Shader_ID: " << shader_id << "  CTA_ID: " << d.CTA_ID << "  Warp_ID: " << idx << " PC: " << d.PC << " Prefetching address: " << a << " Stride " << stride << " Distance " << distance << " C1" << std::endl;
                                                                                                candidates.push_back(std::make_pair(a, idx));
                                                                                        } 
                                                                                }
                                                                        }
                                                                }
                                                                else // Prefetching for other CTAs
                                                                {
                                                                        auto it = p.second.find(d.PC);
                                                                        if(it != p.second.end())
                                                                        {
                                                                                for(unsigned int idx = ((p.first - 1) * d.num_warps); idx < (p.first * d.num_warps); idx++)
                                                                                {
                                                                                        int distance = idx - it->second.leading_warp_id;
                                                                                        if(distance != 0)
                                                                                        {
                                                                                                std::vector<new_addr_type> addrs;
                                                                                                for(new_addr_type base: it->second.base_addresses)
                                                                                                {
                                                                                                        new_addr_type prefetch_candidate = base + (stride * distance);
                                                                                                        addrs.push_back(prefetch_candidate);   
                                                                                                }
                                                                                                std::vector<new_addr_type> coalesced_addresses = this->get_coalesced_addresses(std::move(addrs));
                                                                                                for(new_addr_type& a : coalesced_addresses)
                                                                                                {
                                                                                                        std::cout << "Shader_ID: " << shader_id << "  CTA_ID: " << d.CTA_ID << "  Warp_ID: " << idx << " PC: " << d.PC << " Prefetching address: " << a << " Stride " << stride << " Distance " << distance << " C1" << std::endl;
                                                                                                        candidates.push_back(std::make_pair(a, idx));
                                                                                                } 
                                                                                        }
                                                                                }
                                                                        }
                                                                }
                                                        }
                                                }
                                        }
                                }
                        
                                if(this->Dist_table[d.PC].misprediction_counter >= MISPRED_THRESH && REMOVE_WHEN_misprediction_counter_IS_128) {
                                        // Remove the entry from the Dist table
                                        std::cout << "Shader_ID: " << shader_id << "  Removing entry from Dist table for PC: " << d.PC << " delta : " << this->Dist_table[d.PC].stride;
                                        this->Dist_table.erase(d.PC);
                                        this->print_Dist_table();
                                }
                        }
               }
        }
        
        if(!candidates.empty()) { // Debugging
                std::cout << "Shader_ID: " << shader_id << "  Prefetching address:  ";
                for(auto &prefetch_addr: candidates)
                        std::cout<< prefetch_addr.first << "  ";
                std::cout << "\n";
        }
        for (std::pair<new_addr_type, unsigned int> it : candidates) {
           auto prefetch_requests_it = std::find(this->prefetch_requests.begin(), this->prefetch_requests.end(), it);
           if(prefetch_requests_it != this->prefetch_requests.end()) // If the prefetch request is already in the list, move it to the front
                this->prefetch_requests.erase(prefetch_requests_it);
           this->prefetch_requests.push_front(it); // Add the prefetch request to the front of the list to prioritize latest requests
        }
}
