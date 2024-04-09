#include "../abstract_hardware_model.h"
#include "shader.h"
#include <climits>
#include <list>
#include <map>
#include <set>
#include <utility>

#ifndef __CTA__
#define __CTA__

#define VARIATION           4   // Maximum number of coalesced addresses for a traceable instruction
#define MAX_CTA_TABLE_SIZE  4   // Maximum number of entries in the PerCTA table
#define MAX_DIST_TABLE_SIZE 4   // Maximum number of entries in the Distance table
#define MISPRED_THRESH      128 // Maximum number of allowed mispredictions per PC

namespace CTA_Aware
{

        typedef struct
        {
                        unsigned int  leading_warp_id;
                        new_addr_type base_addresses[VARIATION]; // There can be up to 32 addresses but after coalescing only 4 are left. If there are more than 4, the entry is invalid
        } PerCTA_entry_t;

        typedef struct
        {
                        long long int stride;
                        unsigned int  misprediction_counter; // Limited to MISPRED_THRESH
        } Dist_entry_t;

        typedef struct
        {
                        unsigned int                          CTA_ID;
                        unsigned int                          PC;
                        unsigned int                          WarpID;         // All Warp IDs are unique across the SM, i.e. no two CTAs can have a warp with the same ID
                        std::map<unsigned int, new_addr_type> base_addresses; // Memory addresses for all threads in the warp
        } CTA_data_t;

        class CTA_Aware_Prefetcher
        {
                private:
                        std::map<unsigned int, std::map<unsigned int, PerCTA_entry_t>> PerCTA_table;          // Index 1 = CTA ID, Index 2 = Program Counter. Limited to MAX_CTA_TABLE_SIZE entries
                        std::map<unsigned int, Dist_entry_t>                           Dist_table;            // Index = Program Counter, Limited to MAX_DIST_TABLE_SIZE entries
                        unsigned int                                                   last_serviced_warp_id; // Warp ID of the most recently serviced prefetch
                        std::set<new_addr_type>                                        get_coalesced_addresses(std::map<unsigned int, new_addr_type>& addrs);

                public:
                        void         mark_request_serviced(unsigned int warp_id); // called by LDST Unit. Sets the Warp ID for thw warp which got its prefetch request serviced
                        unsigned int get_warp_id();                    // Called by SM. Returns the Warp ID of the warp for which the prefetch request was serviced most recently. After returning the
                                                                       // value, the last_serviced_warp_id is reset to INVALID
                        void update_state(std::list<CTA_data_t> data); // Called by LDST Unit. LDST informs CTA-Aware about all the addresses accessed by the threads in a warp for all warps
                        std::list<new_addr_type> generate_prefetch_candidates(); // Called by LDST. Returns a list of prefetch candidates
                        const unsigned int       INVALID = UINT_MAX;
                        CTA_Aware_Prefetcher(): last_serviced_warp_id(INVALID) {}
        };
} // namespace CTA_Aware

#endif
