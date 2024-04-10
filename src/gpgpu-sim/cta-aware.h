#include "../abstract_hardware_model.h"
#include "shader.h"
#include <climits>
#include <list>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <utility>
#include <iostream>
#include <cassert>
#include <utility>

#ifndef __CTA__
#define __CTA__

#define VARIATION           4   // Maximum number of coalesced addresses for a traceable instruction
#define MAX_CTA_TABLE_SIZE  4   // Maximum number of entries in the PerCTA table
#define MAX_DIST_TABLE_SIZE 4   // Maximum number of entries in the Distance table
#define MISPRED_THRESH      128 // Maximum number of allowed mispredictions per PC

namespace CTA_Aware
{

        struct PerCTA_entry_t
        {
                        unsigned int  leading_warp_id;
                        std::vector<new_addr_type> base_addresses; // There can be up to 32 addresses but after coalescing only VARIATION number are left
                        PerCTA_entry_t(unsigned int wid, std::vector<new_addr_type>&& ba): leading_warp_id(wid), base_addresses(ba) {}
        };

        struct Dist_entry_t
        {
                        bool is_valid;  // If the entry is invalid, ignore it
                        long long int stride;
                        unsigned int  misprediction_counter; // Limited to MISPRED_THRESH
                        Dist_entry_t(long long int stride): is_valid(false), stride(stride), misprediction_counter(0) {}
        };

        struct CTA_data_t
        {
                        unsigned int                          CTA_ID;
                        unsigned int                          PC;
                        unsigned int                          Warp_ID;        // All Warp IDs are unique across the SM, i.e. no two CTAs can have a warp with the same ID
                        std::vector<new_addr_type> base_addresses; // Memory addresses for all threads in the warp
        };

        class CTA_Aware_Prefetcher
        {
                private:
                        std::map<unsigned int, std::map<unsigned int, PerCTA_entry_t>> PerCTA_table;          // Index 1 = CTA ID, Index 2 = Program Counter. Limited to MAX_CTA_TABLE_SIZE entries
                        std::map<unsigned int, Dist_entry_t>                           Dist_table;            // Index = Program Counter, Limited to MAX_DIST_TABLE_SIZE entries
                        unsigned int                                                   last_serviced_warp_id; // Warp ID of the most recently serviced prefetch
                        std::vector<new_addr_type>                                     get_coalesced_addresses(std::map<unsigned int, new_addr_type>& addrs);
                        bool                                                           in_PerCTA(unsigned int CTA_ID, unsigned int PC);
                        bool                                                           in_Dist(unsigned int PC);
                        std::size_t                                                    num_warps_per_CTA;

                public:
                        void         mark_request_serviced(unsigned int warp_id); // called by LDST Unit. Sets the Warp ID for thw warp which got its prefetch request serviced
                        unsigned int get_warp_id();                    // Called by SM. Returns the Warp ID of the warp for which the prefetch request was serviced most recently. After returning the
                                                                       // value, the last_serviced_warp_id is reset to INVALID
                        std::list<new_addr_type> generate_prefetch_candidates(std::list<CTA_data_t> data); // Called by LDST. Returns a list of prefetch candidates
                        const unsigned int       INVALID = UINT_MAX;
                        CTA_Aware_Prefetcher(unsigned int num_warps): last_serviced_warp_id(INVALID), num_warps_per_CTA(num_warps) {}
        };
} // namespace CTA_Aware

#endif
