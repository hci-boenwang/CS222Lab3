/***************************************************************/
/*                                                             */
/*   ARM Instruction Level Simulator                           */
/*                                                             */
/*   CMSC-22200 Computer Architecture                          */
/*   University of Chicago                                     */
/*                                                             */
/***************************************************************/

#include "bp.h"
#include "pipe.h"
#include <stdlib.h>
#include <stdio.h>

void bp_init(bp_t *bp) {

    bp->btb_size = 1024;
    bp->ghr = 0;
    bp->ghr_bits = 8;
    bp->btb_bits = 10;

    bp->pht = (uint8_t *)calloc(1 << bp->ghr_bits, sizeof(uint8_t));
    bp->btb_tag = (uint64_t *)calloc(bp->btb_size, sizeof(uint64_t));
    bp->btb_dest = (uint64_t *)calloc(bp->btb_size, sizeof(uint64_t));
    bp->btb_valid = (uint8_t *)calloc(bp->btb_size, sizeof(uint8_t));
    bp->btb_cond = (uint8_t *)calloc(bp->btb_size, sizeof(uint8_t));
}

void bp_predict(bp_t *bp, uint64_t PC, uint64_t *pred_PC, uint8_t *btb_miss)
{
    int btb_idx = extract_bits(PC, 2, 11);
    uint8_t pht_idx = bp->ghr ^ extract_bits(PC, 2, 9);
    *btb_miss = 0;

    if (bp->btb_tag[btb_idx] != PC || !bp->btb_valid[btb_idx]) { // miss
        *pred_PC = PC + 4;
        *btb_miss = 1;
        return;
    }

    if (!bp->btb_cond[btb_idx] || bp->pht[pht_idx] > 1) {
        *pred_PC = bp->btb_dest[btb_idx];
    }
    else {
        *pred_PC = PC + 4;
    }
}

void bp_update(bp_t *bp, uint64_t PC, uint8_t cond, uint64_t target_PC, uint8_t branched)
{
    int btb_idx = extract_bits(PC, 2, 11);
    uint8_t pht_idx = bp->ghr ^ extract_bits(PC, 2, 9);

    bp->btb_tag[btb_idx] = PC;
    bp->btb_valid[btb_idx] = 1;
    bp->btb_cond[btb_idx] = cond;
    bp->btb_dest[btb_idx] = target_PC;

    if (cond) {
        bp->ghr = bp->ghr << 1;
        if (branched) {
            if (bp->pht[pht_idx] < 3) {
                bp->pht[pht_idx]++;
            }
            bp->ghr += 1;
        }
        else {
            if (bp->pht[pht_idx] > 0) {
                bp->pht[pht_idx]--;
            }
        }
        bp->ghr = bp->ghr & 0xFF;
    }
}
