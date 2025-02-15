/***************************************************************/
/*                                                             */
/*   ARM Instruction Level Simulator                           */
/*                                                             */
/*   CMSC-22200 Computer Architecture                          */
/*   University of Chicago                                     */
/*                                                             */
/***************************************************************/

#ifndef _BP_H_
#define _BP_H_

#include <stdint.h>

typedef struct
{
    /* gshare */
    int ghr_bits;
    uint32_t ghr;
    uint8_t *pht; /* size 2^ghr_bits */

    /* BTB */
    int btb_size;
    int btb_bits;
    uint64_t *btb_tag;
    uint64_t *btb_dest;
    uint8_t *btb_valid;
    uint8_t *btb_cond;
} bp_t;


void bp_init();

void bp_predict(uint64_t PC, uint64_t *pred_PC, bool *pred_taken);

void bp_update(uint64_t PC, uint8_t cond, uint64_t target_PC, uint8_t branched);

#endif
