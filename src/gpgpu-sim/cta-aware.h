#include "../abstract_hardware_model.h"
#include "shader.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <set>
#include <utility>
#include <vector>

#ifndef __CTA__
#define __CTA__

#define VARIATION           4   // Maximum number of coalesced addresses for a traceable instruction
#define MAX_CTA_TABLE_SIZE  4   // Maximum number of entries in the PerCTA table for each CTA
#define MAX_DIST_TABLE_SIZE 4   // Maximum number of entries in the Distance table
#define MISPRED_THRESH      128 // Maximum number of allowed mispredictions per PC
#define COALESCING_BITS     5   // Number of lower order bits to mask off while coalescing

namespace CTA_Aware
{
        struct PerCTA_entry_t
        {
                unsigned long long         cycle;           // Track the cycle at which this entry was touched last for LRU replacement
                unsigned int               leading_warp_id; // The warp ID of the warp which provided the addresses
                std::vector<new_addr_type> base_addresses;  // There can be up to (some may be inactive) 32 addresses but after
                                                            // coalescing only VARIATION number should be left. Otherwise an entry would not be allocated

                PerCTA_entry_t(): cycle(0), leading_warp_id(std::numeric_limits<unsigned int>::max())
                {
                        this->base_addresses.clear();
                }

                PerCTA_entry_t(unsigned int wid, std::vector<new_addr_type>&& ba, unsigned long long c):
                        cycle(c), leading_warp_id(wid), base_addresses(std::move(ba))
                {}

                PerCTA_entry_t(const PerCTA_entry_t& other)
                {
                        this->cycle = other.cycle;
                        this->leading_warp_id = other.leading_warp_id;
                        this->base_addresses  = other.base_addresses;
                }

                PerCTA_entry_t& operator=(PerCTA_entry_t& other)
                {
                        this->cycle = other.cycle;
                        this->leading_warp_id = other.leading_warp_id;
                        this->base_addresses  = other.base_addresses;
                        return *this;
                }
        };

        struct Dist_entry_t
        {
                unsigned long long cycle;                 // Track the cycle at which this entry was last touched for LRU replacement
                long long int      stride;                // The computed stride
                unsigned int       misprediction_counter; // Limited to MISPRED_THRESH
                unsigned int       correct_counter; 
                long long int      expected_stride;       // The computed stride

                Dist_entry_t(): cycle(0), stride(0), misprediction_counter(0), correct_counter(0), expected_stride(-1) {}

                Dist_entry_t(long long int stride, unsigned long long c):
                        cycle(0), stride(stride), misprediction_counter(0), correct_counter(0), expected_stride(-1)
                {}

                Dist_entry_t(const Dist_entry_t& other)
                {
                        this->cycle                 = other.cycle;
                        this->stride                = other.stride;
                        this->misprediction_counter = other.misprediction_counter;
                        this->correct_counter       = other.correct_counter;
                        this->expected_stride       = other.expected_stride;
                }

                Dist_entry_t& operator=(Dist_entry_t& other)
                {
                        this->cycle                 = other.cycle;
                        this->stride                = other.stride;
                        this->misprediction_counter = other.misprediction_counter;
                        this->correct_counter       = other.correct_counter;
                        this->expected_stride       = other.expected_stride;
                        return *this;
                }
        };

        struct CTA_data_t
        {
                unsigned int               num_warps;      // Number of warps in the CTA
                unsigned int               CTA_ID;         // The ID of the CTA
                new_addr_type              PC;             // The PC value which is associated with base_addresses
                unsigned int               Warp_ID;        // All Warp IDs are unique across the SM, i.e. no two CTAs can have a warp with the same ID
                std::vector<new_addr_type> base_addresses; // Memory addresses for all threads in the warp
                const active_mask_t active_mask;
                const mem_access_byte_mask_t byte_mask;
                new_addr_type                mf_address;

                CTA_data_t(const unsigned int n_warps, unsigned int ctaid, const new_addr_type pc, const unsigned int wid, const std::vector<new_addr_type>&& ba, const active_mask_t active_mask,
                const mem_access_byte_mask_t byte_mask, new_addr_type mf_address):
                        num_warps(n_warps), CTA_ID(ctaid), PC(pc), Warp_ID(wid), base_addresses(std::move(ba)), active_mask(active_mask), byte_mask(byte_mask), mf_address(mf_address)
                {}
        };

        class CTA_Aware_Prefetcher
        {
                private:
                        unsigned                                                       shader_id;             // The ID of the shader
                        std::map<unsigned int, std::map<unsigned int, PerCTA_entry_t>> PerCTA_table;          // Index 1 = CTA ID, Index 2 = Program Counter. Limited to MAX_CTA_TABLE_SIZE entries
                        std::map<unsigned int, Dist_entry_t>                           Dist_table;            // Index = Program Counter, Limited to MAX_DIST_TABLE_SIZE entries
                        unsigned int                                                   last_serviced_warp_id; // Warp ID of the most recently serviced prefetch
                        std::map<unsigned int, new_addr_type>                          warp_base_addr;        // To keep track of the base address of each warp to prevent false repetition

                        std::vector<new_addr_type> get_coalesced_addresses(const std::vector<new_addr_type>&& addrs) const;
                        new_addr_type get_coalesced_addresses(new_addr_type addrs) const;

                        bool                       in_PerCTA(const unsigned int CTA_ID, const unsigned int PC) const;              // Checks whether there is an entry for CTA_ID and PC in the PerCTA table
                        std::size_t                size_of_PerCTA(unsigned CTA_ID) const;                                          // Returns the number of PCs for a CTA in the PerCTA table
                        void                       insert_in_PerCTA(unsigned int CTA_ID, unsigned int PC, PerCTA_entry_t&& entry); // Inserts an entry for a given CTA and PC in the PerCTA table
                        void                       print_PerCTA_table() const;

                        bool                       in_Dist(unsigned int PC) const;                        // Checks whether there is an entry for a PC in the Dist table
                        std::size_t                size_of_Dist() const;                                  // Returns the size of the Dist table
                        void                       insert_in_Dist(unsigned int PC, Dist_entry_t&& entry); // Inserts an entry for a given PC in the Dist table
                        void                       print_Dist_table() const;

                public:
                        const unsigned int INVALID = std::numeric_limits<unsigned int>::max();
                        std::list<std::pair<new_addr_type, unsigned int>> prefetch_requests;   // List of prefetch requests. Each request is a pair of the address and the warp ID
                        long unsigned num_prefetch_requests = 0;
                        

                        std::set<std::pair<new_addr_type, unsigned int>> prefetch_send; // List of prefetch requests which have been sent to the memory system

                        void         mark_request_serviced(unsigned int warp_id); // called by LDST Unit. Sets the Warp ID for the warp which got its prefetch request serviced
                        unsigned int get_warp_id(); // Called by SM. Returns the Warp ID of the warp for which the prefetch request was serviced most recently. After returning the
                                                    // value, the last_serviced_warp_id is reset to INVALID

                        void generate_prefetch_candidates(std::list<CTA_data_t> data, unsigned long long cycle); // Called by LDST. Returns a list of prefetch candidates

                        CTA_Aware_Prefetcher(): last_serviced_warp_id(INVALID) {}
                        CTA_Aware_Prefetcher(unsigned shader_id): last_serviced_warp_id(INVALID), shader_id(shader_id) {}
        };
} // namespace CTA_Aware

#endif
