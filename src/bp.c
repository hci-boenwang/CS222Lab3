/***************************************************************/
/*                                                             */
/*   ARM Instruction Level Simulator                           */
/*                                                             */
/*   CMSC-22200 Computer Architecture                          */
/*   University of Chicago                                     */
/*                                                             */
/***************************************************************/

#include "bp.h"
#include <stdlib.h>
#include <stdio.h>

bt_p bp;

void bp_init() {

    bp.btb_size = 1024;
    bp.ghr = 0;
    bp.ghr_bits = 8;
    bp.btb_bits = 10;

    bp.pht = (uint8_t *)calloc(1 << bp.ghr_bits, sizeof(uint8_t));
    bp.btb_tag = (uint64_t *)calloc(bp.btb_size, sizeof(uint64_t));
    bp.btb_dest = (uint64_t *)calloc(bp.btb_size, sizeof(uint64_t));
    bp.btb_valid = (uint8_t *)calloc(bp.btb_size, sizeof(uint8_t));
    bp.btb_cond = (uint8_t *)calloc(bp.btb_size, sizeof(uint8_t));
}

void bp_predict(uint64_t PC, uint64_t *pred_PC, bool *pred_taken)
{
    int btb_idx = extract_bits(PC, 2, 11);
    uint8_t pht_idx = bp.ghr ^ extract_bits(PC, 2, 9);
    *pred_taken = FALSE;

    if (bp.btb_tag[btb_idx] != PC || !btb_valid[btb_idx]) { // miss
        *pred_pc = PC + 4;
        return;
    }

    if (!bp.btb_cond[btb_idx] || bp.pht[pht_idx] > 1) {
        *pred_PC = bp.btb_dest[btb_idx];
        *pred_taken = TRUE;
    }
    else {
        *pred_pc = PC + 4;
    }
}

void bp_update(uint64_t PC, uint8_t cond, uint64_t target_PC, uint8_t branched)
{
    int btb_idx = extract_bits(PC, 2, 11);
    uint8_t pht_idx = bp.ghr ^ extract_bits(PC, 2, 9);

    bp.btb_tag[btb_idx] = PC;
    bp.btb_valid[btb_idx] = 1;
    bp.btb_cond[btb_idx] = cond;
    bp.btb_dest[btb_idx] = target_PC;

    if (cond) {
        bp.ghr = bp.ghr << 1;
        if (branched) {
            if (bp.pht[pht_idx] < 3) {
                bp.pht[pht_idx]++;
            }
            bp.ghr += 1
        }
        else {
            if (bp.pht[pht_idx] > 0) {
                bp.pht[pht_idx]--;
            }
        }
        bp.ghr = bp.ghr & 0xFF;
    }
}
