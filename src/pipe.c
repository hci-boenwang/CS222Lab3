/***************************************************************/
/*                                                             */
/*   ARM Instruction Level Simulator                           */
/*                                                             */
/*   CMSC-22200 Computer Architecture                          */
/*   University of Chicago                                     */
/*                                                             */
/***************************************************************/

#include "pipe.h"
#include "shell.h"
#include "bp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/* global pipeline state */
Pipe_State pipe;
Pipe_Reg_F_D preg_F_D;
Pipe_Reg_D_X preg_D_X;
Pipe_Reg_X_M preg_X_M;
Pipe_Reg_M_W preg_M_W;

int RUN_BIT;
bool last_instruction;
bool increment_decode_halt = false;
int stall_count = 0;
bool stall_pending = false;
bool fetch_is_instr = true;
bool flush_pipeline = false;


void handle_r(uint32_t instruction); //helper functions to help with handling all sorts of the different formats
void handle_i(uint32_t instruction);
void handle_d(uint32_t instruction);
void handle_b(uint32_t instruction);
void handle_cb(uint32_t instruction);
void handle_iw(uint32_t instruction);
uint64_t sign_extend(uint64_t imm, uint32_t sign_bit);
uint64_t extract_bits(uint32_t instruction, uint32_t start, uint32_t end);

void pipe_init()
{
    memset(&pipe, 0, sizeof(Pipe_State));
	memset(&preg_F_D, 0, sizeof(Pipe_Reg_F_D));
	memset(&preg_D_X, 0, sizeof(Pipe_Reg_D_X));
	memset(&preg_X_M, 0, sizeof(Pipe_Reg_X_M));
	memset(&preg_M_W, 0, sizeof(Pipe_Reg_M_W));
    bp_init();
    pipe.PC = 0x00400000;
	RUN_BIT = true;
}

void pipe_cycle()
{
    static int cycle_count = 0; //DEBUG
    printf("\n==== Cycle %d ====\n", cycle_count++); //DEBUG

	pipe_stage_wb();
	pipe_stage_mem();
	pipe_stage_execute();
	pipe_stage_decode();
	pipe_stage_fetch();
}



void pipe_stage_wb()
{
    printf("INSTRUCTIONS RETIRED: %d\n", stat_inst_retire);
    if(!preg_M_W.is_instr) { //IF no instruction in this stage
        printf("[NOTHING HERE NO INSTRUCTIOON]\n");
        return;
    }
    stat_inst_retire += 1;
    if (preg_M_W.is_halt) {
        printf("[PIPELINE] All instructions retired. Halting simulation.\n");
        RUN_BIT = 0;
        return;
    }
    if (preg_M_W.is_branch) {
        printf("BRANCH RETIRED\n");
        preg_M_W.is_branch = false; //This is fine because the next instruction cant be a branch due to stalling
        return;
    }
    if (!preg_M_W.op.RegWrite) {
        printf("[WRITEBACK] No register write.\n");
        preg_M_W.op.RegWrite = 0;
        return;
    }
    // Forward from WB stage (MEM/WB → EX) [SEEMS TO WORK NOW]
    if (preg_M_W.op.RegWrite) {
        printf("[WRITEBACK] WE ENTER MEM/WB FORWARD\n");
        if (preg_D_X.is_stur) {
            printf("[WRITEBACK TRY to stur] MWDest: %d and DXdest: %d\n", preg_M_W.dest, preg_D_X.dest);
            if ((preg_M_W.dest == preg_D_X.dest) && (preg_M_W.dest != 31)) {
                printf("[WRITEBACK to stur] Dest: %d, set to REG2: %lx\n", preg_M_W.dest, preg_M_W.ALU_result);
                preg_D_X.reg2_val = preg_M_W.ALU_result;
            }
        }
        if ((preg_M_W.dest == preg_D_X.reg1) && (preg_M_W.dest != 31)) {
            printf("[WRITEBACK] ReferringVal: %d, REG1: %d\n", preg_M_W.dest, preg_D_X.reg1);
            if (preg_M_W.op.MemtoReg) { //IF FORWARDING FROM A LOAD
                printf("[WRITEBACK] from load] Destination: %d, REG1Val: %lx\n", preg_M_W.dest, preg_M_W.mem_read_val);
                preg_D_X.reg1_val = preg_M_W.mem_read_val;
            }
            else {
                preg_D_X.reg1_val = preg_M_W.ALU_result;
            }
            printf("FORWARDING3 %lx\n", preg_D_X.reg1_val);
        }
        if ((preg_M_W.dest == preg_D_X.reg2) && (preg_M_W.dest != 31)) {
            if (preg_M_W.op.MemtoReg) {
                preg_D_X.reg2_val = preg_M_W.mem_read_val;
                printf("FORWARDEDYAY\n");
            }
            else {
                preg_D_X.reg2_val = preg_M_W.ALU_result;
                printf("FORWARDING4 reg2:%d dest: %d, %lx\n", preg_D_X.reg2, preg_M_W.dest, preg_M_W.ALU_result);
            }
        }
    }
    if (preg_M_W.op.MemtoReg) { //ldur
        printf("[WRITEBACK] Register X%d updated to: 0x%lx\n", preg_M_W.dest, preg_M_W.mem_read_val);
		pipe.REGS[preg_M_W.dest] = preg_M_W.mem_read_val;
        preg_M_W.op.MemtoReg = 0;
        preg_X_M.op.MemtoReg = 0;
	}
	else if (preg_M_W.dest != 31){
		pipe.REGS[preg_M_W.dest] = preg_M_W.ALU_result;
        printf("[WRITEBACK2] Register X%d updated to: 0x%lx\n", preg_M_W.dest, pipe.REGS[preg_M_W.dest]);
	}

    if (preg_M_W.flag_set) {
        pipe.FLAG_N = preg_M_W.FLAG_N;
        pipe.FLAG_Z = preg_M_W.FLAG_Z;
        preg_D_X.FLAG_N = pipe.FLAG_N;
        preg_D_X.FLAG_Z = pipe.FLAG_Z;
    }
    preg_M_W.op.RegWrite = 0;
}

void pipe_stage_mem()
{
    preg_M_W.is_instr = preg_X_M.is_instr;
    preg_M_W.is_halt = preg_X_M.is_halt;
    if (preg_X_M.is_halt) {
        return;
    }
    if(!preg_X_M.is_instr) { //IF no instruction in this stage
        return;
    }
    if (preg_X_M.is_branch) {
        preg_M_W.is_branch = preg_X_M.is_branch;
        preg_X_M.is_branch = false; //This is fine because the next instruction cant be a branch due to stalling
        return;
    }
	if (preg_X_M.op.MemRead) { //LDUR (need to remember deassert ops?)
        if (preg_X_M.mem_size ==  64) { //FOR 64 BITS
            uint32_t low = mem_read_32(preg_X_M.ALU_result);        // Read lower 32 bits
            uint32_t high = mem_read_32(preg_X_M.ALU_result + 4);   // Read upper 32 bits
            preg_M_W.mem_read_val = ((uint64_t)high << 32) | low;   // Combine into 64-bit value
            printf("LDURB0 %lx\n", preg_M_W.mem_read_val);
            printf("low: %d, high: %d \n", low, high);
        } else if (preg_X_M.mem_size ==  32) {
            preg_M_W.mem_read_val = mem_read_32(preg_X_M.ALU_result);
            printf("LDURB1 %lx\n", preg_M_W.mem_read_val);
        } else if (preg_X_M.mem_size == 16) {
            preg_M_W.mem_read_val = mem_read_32(preg_X_M.ALU_result) & 0xFFFF;
            printf("LDURB2 %lx\n", preg_M_W.mem_read_val);
        } else if (preg_X_M.mem_size == 8) {
            preg_M_W.mem_read_val = mem_read_32(preg_X_M.ALU_result) & 0xFF;
            printf("LDURB3 %lx\n", preg_M_W.mem_read_val);
        }
        preg_X_M.op.MemtoReg = 1;
        printf("[MEMORY] Loaded Value: 0x%lx from Addr: 0x%lx\n", preg_M_W.mem_read_val, preg_X_M.ALU_result);
        printf("[MEMORY] ALU Result: 0x%lx | Mem Read: %d | Mem Write: %d MWMemtoreg:%d\n", preg_X_M.ALU_result, preg_X_M.op.MemRead, preg_X_M.op.MemWrite,preg_X_M.op.MemtoReg);
        preg_X_M.op.MemRead = 0;
	}
	else if (preg_X_M.op.MemWrite) { //STUR
        printf("STURRRR\n");
        if (preg_X_M.mem_size ==  64) {
            // Split 64-bit value into two 32-bit values
            uint32_t low = (uint32_t)(preg_X_M.reg2_val & 0xFFFFFFFF);   // Lower 32 bits
            uint32_t high = (uint32_t)((preg_X_M.reg2_val >> 32) & 0xFFFFFFFF); // Upper 32 bits
            // Write the lower 32 bits to memory
            mem_write_32(preg_X_M.ALU_result, low);
            // Write the upper 32 bits to the next memory address (word-aligned)
            mem_write_32(preg_X_M.ALU_result + 4, high);
            printf("[MEMORY] Stored 64-bit Value: 0x%lx at Addr: 0x%lx (low: 0x%x, high: 0x%x)\n",
                preg_X_M.reg2_val, preg_X_M.ALU_result, low, high);
        } else if (preg_X_M.mem_size ==  32) {
            mem_write_32(preg_X_M.ALU_result, preg_X_M.reg2_val);
            printf("[MEMORY] Stored Value: 0x%lx to Addr: 0x%lx\n", preg_X_M.reg2_val, preg_X_M.ALU_result);
        } else if (preg_X_M.mem_size ==  16) {
            uint32_t data = mem_read_32(preg_X_M.ALU_result);
            data = ((data & ~0xFFFF) | (preg_X_M.reg2_val & 0xFFFF));
            mem_write_32(preg_X_M.ALU_result, data);
        } else if (preg_X_M.mem_size ==  8) {
            printf("WE HAVE STURRB\n");
            uint32_t data = mem_read_32(preg_X_M.ALU_result);
            data = ((data & ~0xFF) | (preg_X_M.reg2_val & 0xFF));
            mem_write_32(preg_X_M.ALU_result, data);
            printf("[MEMORY] Reg2 Value: 0x%lx to Addr: 0x%lx, data: %d\n", preg_X_M.reg2_val, preg_X_M.ALU_result, data);
        }
        preg_X_M.op.MemWrite = 0;
        preg_X_M.op.RegWrite = 0;
    }
    // Forward from MEM stage (EX/MEM → EX) (LDUR instructions)
    if (preg_X_M.op.RegWrite) { //SEEMS LIKE THIS IS WORKING CORRECTLY FOR NOW reg1 should not be 1 but 4 i think for 1b cycle 7 need to figure out
        printf("WE ENTER EX/MEM FORWARD\n");
        printf("reg1 %d, dest: %d\n", preg_D_X.reg1, preg_X_M.dest);
        if (preg_D_X.is_branch && preg_X_M.flag_set) {
            preg_D_X.FLAG_N = preg_X_M.FLAG_N;
            preg_D_X.FLAG_Z = preg_X_M.FLAG_Z;
        }  
        
        if (preg_D_X.is_stur) {
            if (preg_X_M.dest == preg_D_X.dest) { //THIS IS making it if xm dest is not a stur, but if it is a stur we do smth else
                preg_D_X.reg2_val = preg_X_M.ALU_result;
            }
        }
        
        if ((preg_X_M.dest == preg_D_X.reg1) && (preg_X_M.dest != 31)) {
            if (preg_X_M.op.MemtoReg) {
                preg_D_X.reg1_val = preg_M_W.mem_read_val;
            }
            else {
                preg_D_X.reg1_val = preg_X_M.ALU_result;
                printf("FORWARDING %lx\n", preg_D_X.reg1_val);
            }
        }
        if ((preg_X_M.dest == preg_D_X.reg2) && (preg_X_M.dest != 31)) {
            if (preg_X_M.op.MemtoReg) {
                preg_D_X.reg2_val = preg_M_W.mem_read_val;
            }
            else {
                preg_D_X.reg2_val = preg_X_M.ALU_result;
            }
            printf("FORWARDING2 %lx\n", preg_D_X.reg2_val);
        }
    }
    printf("regwrite %d, dest: %d\n", preg_X_M.op.RegWrite, preg_X_M.dest);
    printf("xmdest:%d, mwdest:%d\n", preg_X_M.dest, preg_M_W.dest);
    preg_M_W.dest = preg_X_M.dest;
    preg_M_W.ALU_result = preg_X_M.ALU_result; 
    preg_M_W.op = preg_X_M.op;
    preg_M_W.is_stur = preg_X_M.is_stur;
    preg_M_W.FLAG_N = preg_X_M.FLAG_N;
    preg_M_W.FLAG_Z = preg_X_M.FLAG_Z;
    preg_M_W.flag_set = preg_X_M.flag_set;
}

void pipe_stage_execute() // this stage is combining the write back and stuff so need to change this
{
    printf("opcode %d\n", preg_D_X.opcode);
    printf("[post stur ]Reg2val: %lx\n", preg_D_X.reg2_val);
    printf("reg1: %d, reg2: %d\n", preg_D_X.reg1, preg_D_X.reg2);
    uint8_t cond = 0;
    uint8_t branched = 0;
    uint64_t target_PC = 0;

    preg_X_M.is_instr = preg_D_X.is_instr;
    if(!preg_D_X.is_instr) { //IF no instruciton at this stage
        preg_X_M.dest = 0;
        return;
    }
    preg_X_M.flag_set = false;
	if (preg_D_X.opcode == 0x6a2) { // HLT
        printf("[EXECUTE] HLT Executed. Halting pipeline.\n");
    } else if (preg_D_X.opcode == 0x458) { // ADD (Done)
        printf("ADD?\n");
		preg_X_M.ALU_result = preg_D_X.reg1_val + preg_D_X.reg2_val;
        preg_X_M.op.RegWrite = 1;
    } else if (preg_D_X.opcode == 0x488 || preg_D_X.opcode == 0x489) { //ADDI (Done)
        printf("ADDI?\n");
        preg_X_M.ALU_result = preg_D_X.reg1_val + preg_D_X.imm;
        preg_X_M.op.RegWrite = 1;
    } else if (preg_D_X.opcode == 0x558) { // ADDS (Done, likely will need to forward/deassert flags)
        preg_X_M.ALU_result = preg_D_X.reg1_val + preg_D_X.reg2_val;
        preg_X_M.op.RegWrite = 1;
        preg_X_M.FLAG_Z = (preg_X_M.ALU_result == 0);
        preg_X_M.FLAG_N = (preg_X_M.ALU_result < 0);
        preg_X_M.flag_set = true;
        printf("ADDS: FLAG_Z=%d | FLAG_N=%d\n", preg_X_M.FLAG_Z, preg_X_M.FLAG_N);
    } else if (preg_D_X.opcode == 0x588 || preg_D_X.opcode == 0x589) { //ADDIS (DONE)
        preg_X_M.ALU_result = preg_D_X.reg1_val + preg_D_X.imm;
        preg_X_M.op.RegWrite = 1;
        preg_X_M.FLAG_Z = (preg_X_M.ALU_result == 0); 
        printf("ADDIS: FLAG_Z=%d | FLAG_N=%d | reg1val: %lx, immediate: %lx, ALU_RESULT = %lx\n", preg_X_M.FLAG_Z, preg_X_M.FLAG_N, preg_D_X.reg1_val, preg_D_X.imm, preg_X_M.ALU_result);
        preg_X_M.FLAG_N = (preg_X_M.ALU_result < 0);
        preg_X_M.flag_set = true;
    } else if (preg_D_X.opcode >= 0x5A8 && preg_D_X.opcode <= 0x5AF) { // CBNZ (Done)
        printf("CBNZ: address:%lx\n", preg_D_X.addr);
        cond = 1;
        target_PC = preg_D_X.addr;
        preg_F_D.is_instr = false; 
        if (preg_D_X.addr != preg_D_X.next_PC) {//IF THE ADDRESS BRANCHING IS NOT THE SAME AS OUR NEXT ADDRESS
            if (preg_D_X.reg2_val != 0) { 
                branched = 1;
                pipe.PC = preg_D_X.addr;
                fetch_is_instr = false; 
            }
        }
    } else if (preg_D_X.opcode >= 0x5A0 && preg_D_X.opcode <= 0x5A7) { // CBZ (Done)
        printf("CBZ: address:%lx\n", preg_D_X.addr);
        cond = 1;
        target_PC = preg_D_X.addr;
        preg_F_D.is_instr = false; 
        if (preg_D_X.addr != preg_D_X.next_PC) {//IF THE ADDRESS BRANCHING IS NOT THE SAME AS OUR NEXT ADDRESS
            if (preg_D_X.reg2_val == 0) {
                branched = 1; 
                pipe.PC = preg_D_X.addr;
                fetch_is_instr = false; 
            }
        }

    } else if (preg_D_X.opcode == 0x450) { // AND (Done)
		preg_X_M.ALU_result = preg_D_X.reg1_val & preg_D_X.reg2_val;
        preg_X_M.op.RegWrite = 1;
    } else if (preg_D_X.opcode == 0x750) { // ANDS (Done)
		preg_X_M.ALU_result = preg_D_X.reg1_val & preg_D_X.reg2_val;
        preg_X_M.op.RegWrite = 1;
        preg_X_M.FLAG_Z = (preg_X_M.ALU_result == 0);
        preg_X_M.FLAG_N = (preg_X_M.ALU_result < 0);
        preg_X_M.flag_set = true;
    } else if (preg_D_X.opcode == 0x650) { // EOR (Done)
        preg_X_M.ALU_result = preg_D_X.reg1_val ^ preg_D_X.reg2_val;
        preg_X_M.op.RegWrite = 1;
    } else if (preg_D_X.opcode == 0x550) { // ORR (Done)
		preg_X_M.ALU_result = preg_D_X.reg1_val | preg_D_X.reg2_val;
        preg_X_M.op.RegWrite = 1;
	} else if (preg_D_X.opcode == 0x7C2) { // LDUR (DONE)
        preg_X_M.ALU_result = preg_D_X.reg1_val + preg_D_X.imm;
        preg_X_M.op.RegWrite = 1;
        preg_X_M.op.MemRead = 1;
        preg_X_M.mem_size = 64; //Changed from 32 to 64
    } else if (preg_D_X.opcode == 0x5C2) { // LDUR 32 bit variant (DONE)
        preg_X_M.ALU_result = preg_D_X.reg1_val + preg_D_X.imm;
        preg_X_M.op.RegWrite = 1;
        preg_X_M.op.MemRead = 1;
        preg_X_M.mem_size = 32; //Changed from 32 to 64
    } else if (preg_D_X.opcode == 0x1C2) { // LDURB (Done)
        preg_X_M.ALU_result = preg_D_X.reg1_val + preg_D_X.imm;
        preg_X_M.op.RegWrite = 1;
        preg_X_M.op.MemRead = 1;
        preg_X_M.mem_size = 8;
    } else if (preg_D_X.opcode == 0x3C2) { // LDURH (Done)
        preg_X_M.ALU_result = preg_D_X.reg1_val + preg_D_X.imm;
        preg_X_M.op.RegWrite = 1;
        preg_X_M.op.MemRead = 1;
        preg_X_M.mem_size = 16;
    } else if (preg_D_X.opcode == 0x69A || preg_D_X.opcode == 0x69B) {
        if (preg_D_X.imms == 0x3f) { //LSRI (Done)
            preg_X_M.ALU_result = preg_D_X.reg1_val >> preg_D_X.immr;
        } else { //LSLI (Done)
            printf("reg1val:%lx, immediate:%d\n", preg_D_X.reg1_val, (63 - preg_D_X.imms));
            preg_X_M.ALU_result = preg_D_X.reg1_val << (63 - preg_D_X.imms);
        }
        preg_X_M.op.RegWrite = 1;
    }
    else if (preg_D_X.opcode >= 0x694 && preg_D_X.opcode <= 0x697) { // MOVZ (Done)
        preg_X_M.ALU_result = preg_D_X.imm << preg_D_X.mov_shift;
        preg_X_M.op.RegWrite = 1;
    } else if (preg_D_X.opcode == 0x7C0) { // STUR (Done)
        preg_X_M.ALU_result = preg_D_X.reg1_val + preg_D_X.imm;
        preg_X_M.op.MemWrite = 1;
        preg_X_M.reg2_val = preg_D_X.reg2_val;
        preg_X_M.mem_size = 64; //changed from 32 to 64
    } else if (preg_D_X.opcode == 0x5C0) { // STUR 32 bit variant (Done)
        preg_X_M.ALU_result = preg_D_X.reg1_val + preg_D_X.imm;
        preg_X_M.op.MemWrite = 1;
        preg_X_M.reg2_val = preg_D_X.reg2_val;
        preg_X_M.mem_size = 32; //changed from 32 to 64
    } else if (preg_D_X.opcode == 0x1C0) { // STURB (Done)
        preg_X_M.ALU_result = preg_D_X.reg1_val + preg_D_X.imm;
        preg_X_M.op.MemWrite = 1;
        preg_X_M.reg2_val = preg_D_X.reg2_val;
        preg_X_M.mem_size = 8;
    } else if (preg_D_X.opcode == 0x3C0) { // STURH (Done)
        preg_X_M.ALU_result = preg_D_X.reg1_val + preg_D_X.imm;
        preg_X_M.op.MemWrite = 1;
        preg_X_M.reg2_val = preg_D_X.reg2_val;
        preg_X_M.mem_size = 16;
    } else if (preg_D_X.opcode == 0x658) { // SUB (Done)
        preg_X_M.ALU_result = preg_D_X.reg1_val - preg_D_X.reg2_val;
        preg_X_M.op.RegWrite = 1;
    } else if (preg_D_X.opcode == 0x688 || preg_D_X.opcode == 0x689){ //SUBI (Done)
        preg_X_M.ALU_result = preg_D_X.reg1_val - preg_D_X.imm;
        preg_X_M.op.RegWrite = 1;
    }else if (preg_D_X.opcode == 0x758) { // SUBS (Done)
        int64_t result = preg_D_X.reg1_val - preg_D_X.reg2_val;
        preg_X_M.FLAG_Z = (result == 0);
        preg_X_M.FLAG_N = (result < 0);
        preg_X_M.flag_set = true;
        //Handles CMP
        if (preg_D_X.dest != 31) { // XZR is register 31
            preg_X_M.ALU_result = result;
            preg_X_M.op.RegWrite = 1;
        }
    } else if (preg_D_X.opcode == 0x788 || preg_D_X.opcode == 0x789) { // SUBIS (Done)
        int64_t result = preg_D_X.reg1_val - preg_D_X.imm;
        preg_X_M.FLAG_Z = (result == 0);
        preg_X_M.FLAG_N = (result < 0);
        preg_X_M.flag_set = true;
        //Handles CMPI
        if (preg_D_X.dest != 31) { // XZR is register 31
            preg_X_M.ALU_result = result;
            preg_X_M.op.RegWrite = 1;
        }
    } else if (preg_D_X.opcode == 0x4D8) { // MUL (Done)
        preg_X_M.ALU_result = preg_D_X.reg1_val * preg_D_X.reg2_val;
        preg_X_M.op.RegWrite = 1;
    } else if (preg_D_X.opcode == 0x6B0) { // BR (Done?)]
        preg_F_D.is_instr = false;
        target_PC = preg_D_X.reg2_val;
        if (preg_D_X.reg2_val != preg_D_X.next_PC) {//IF THE ADDRESS BRANCHING IS NOT THE SAME AS OUR NEXT ADDRESS
            branched = 1;
            pipe.PC = preg_D_X.reg2_val;
            fetch_is_instr = false;
        }
    } else if (preg_D_X.opcode >= 0x0A0 && preg_D_X.opcode <= 0x0BF) { // B (Done)
        preg_F_D.is_instr = false; 
        target_PC = preg_D_X.addr;
        printf("dxaddr: %lx, pipe.pc: %lx", preg_D_X.addr, preg_D_X.next_PC);
        if (preg_D_X.addr != preg_D_X.next_PC) {//IF THE ADDRESS BRANCHING IS NOT THE SAME AS OUR NEXT ADDRESS
            branched = 1;
            pipe.PC = preg_D_X.addr;
            printf("SO ITS A DIFF ADDRESS\n");
            fetch_is_instr = false;
        }
    } else if (preg_D_X.opcode >= 0x2A0 && preg_D_X.opcode <= 0x2A7) { // BEQ, BNE, BGT, BLT, BGE, BLE
        // Handle conditional branches
        preg_F_D.is_instr = false; 
        cond = 1;
        target_PC = preg_D_X.addr;
        uint32_t condition_met = 0;
        if (preg_D_X.rt == 0x0) { //BEQ
            condition_met = (preg_D_X.FLAG_Z == 1); //FORWARD THESE FLAGS
        } else if (preg_D_X.rt == 0x1) { //BNE
            condition_met = (preg_D_X.FLAG_Z == 0);
        } else if (preg_D_X.rt == 0xC) { //BGT
            condition_met = (preg_D_X.FLAG_Z == 0 && preg_D_X.FLAG_N == 0);
        } else if (preg_D_X.rt == 0xB) { //BLT
            condition_met = (preg_D_X.FLAG_N == 1);
        } else if (preg_D_X.rt == 0xA) { //BGE
            condition_met = (preg_D_X.FLAG_N == 0);
        } else if (preg_D_X.rt == 0xD) { //BLE
            condition_met = (preg_D_X.FLAG_Z == 1 || preg_D_X.FLAG_N == 1);
        }
        if (condition_met && preg_D_X.addr != preg_D_X.next_PC) { //branch if condition is met and not same address
            branched = 1;
            pipe.PC = preg_D_X.addr; 
            fetch_is_instr = false;
        }
    }
    if (preg_D_X.is_branch) {
        bp_update(preg_D_X.instr_PC, cond, target_PC, branched);
        if (branched) {
            flush_pipeline = true;
        }
    }
    preg_X_M.is_stur = preg_D_X.is_stur;
    preg_X_M.is_branch = preg_D_X.is_branch;
    preg_X_M.is_halt = preg_D_X.is_halt;
    printf("xmdest %d, dxdest %d", preg_X_M.dest, preg_D_X.dest);
    preg_X_M.dest = preg_D_X.dest;
    printf("[EXECUTE] Opcode: 0x%x | ALU Inputs: 0x%lx, 0x%lx | ALU Result: 0x%lx | Dest: X%d\n", preg_D_X.opcode, preg_D_X.reg1_val, preg_D_X.reg2_val, preg_X_M.ALU_result, preg_D_X.dest);
}

void pipe_stage_decode()
{
    if (flush_pipeline) {
        preg_D_X.is_instr = false;
        return;
    }
    preg_D_X.is_instr = preg_F_D.is_instr;
    preg_D_X.instr_PC = preg_F_D.instr_PC;
    if(!preg_F_D.is_instr) { //IF THE INSTRUCTION IS NOT AN ACTUAL INSTRUCTION YET
        return;
    }
    if (stall_count > 0) {
        printf("[DECODE] Stalled, instruction held.\n");
        preg_D_X.is_instr = true;
        return;
    }
    printf("[PRESTALL INFO] MEMRead: %x | LDUR destination: X%d | Decode needs: X%d or X%d\n", preg_X_M.op.MemRead, preg_X_M.dest, preg_D_X.reg1, preg_D_X.reg2);
    preg_D_X.opcode = extract_bits(preg_F_D.instruction, 21, 31);
	if (preg_F_D.instruction == 0xD4400000) {  // HLT instruction has a fixed encoding
        preg_D_X.is_halt = true;
        preg_D_X.is_instr = true;
        last_instruction = true;
        increment_decode_halt = true;
        printf("[DECODE] HLT Detected! Stopping pipeline.\n");
        printf("opcode %d\n", preg_D_X.opcode);
        return;
    }
    preg_D_X.is_stur = false;
    preg_D_X.is_load = false;
    preg_D_X.is_branch = false;
    stall_pending = false;
    uint32_t op0 = extract_bits(preg_F_D.instruction, 25, 28); // Extract bits 28–25
    if ((op0 & 0b1110) == 0b1000) { // Data Processing - Immediate (I-format) mask into 100x
        uint32_t iw_opcode = extract_bits(preg_F_D.instruction, 23, 25); // Extract bits 25-23
        if ((iw_opcode & 0b111) == 0b101) { //mask bits 25-28 into 101
            handle_iw(preg_F_D.instruction); // IW Format (MOVZ, MOVK, MOVN)
            printf("DECODE REG1 check for DX: forwarding: %d\n", preg_D_X.reg1);
        } else {
            handle_i(preg_F_D.instruction); // Other I-format instructions
        }
    } else if ((op0 & 0b1110) == 0b1010) { // Branches and Exception Generating (B/CB-format)
        stall_pending = true;  // this is set for branching
        if ((extract_bits(preg_F_D.instruction, 29, 30) == 0b01) && (extract_bits(preg_F_D.instruction, 25, 25) == 0)) {  //to differentiate CBZ CBNZ
            handle_cb(preg_F_D.instruction); // Conditional Branches
        } else if ((extract_bits(preg_F_D.instruction, 29, 31) == 0b010) && (extract_bits(preg_F_D.instruction, 25, 25) == 0)) {
            handle_cb(preg_F_D.instruction);
        } 
        else {
            handle_b(preg_F_D.instruction); // Unconditional Branches
        }
    } else if ((op0 & 0b0101) == 0b0100) { // Loads and Stores (D-format) mask into x1x0
        handle_d(preg_F_D.instruction);
    } else if ((op0 & 0b0111) == 0b0101) { // Data Processing - Register (R-format) //mask to see if x101
        handle_r(preg_F_D.instruction);
    }
    printf("[PRESTALL INFO2] MEMRead: %x | LDUR destination: X%d | Decode needs: X%d or X%d\n",
        preg_X_M.op.MemRead, preg_X_M.dest, preg_D_X.reg1, preg_D_X.reg2);
    if (preg_X_M.op.MemRead && (preg_X_M.dest == preg_D_X.reg1 || preg_X_M.dest == preg_D_X.reg2) && !preg_D_X.is_load) { //LOAD HAZARD
        printf("[STALL] Load-use hazard detected. Stalling pipeline.\n");
        // Hold the instruction in Decode, and prevent Fetch from updating
        stall_pending = true;
        preg_D_X.is_instr = false;
        return;
    }
    printf("[DECODE] PC: 0x%lx | Instr: 0x%08x | Opcode: 0x%x\n", preg_F_D.instr_PC, preg_F_D.instruction, preg_D_X.opcode); //DEBUG
    preg_D_X.next_PC = preg_F_D.next_PC;
}

void pipe_stage_fetch()
{
    if (flush_pipeline) {
        flush_pipeline = false;
        return;
    }

    if (stall_pending) { //branch stall (we stall after fetch for one stage)
        stall_pending = false;
        stall_count++;
    } else if (stall_count > 0) { //STALLING
        stall_count --;
        printf("STALLING one cycle\n");
        if (fetch_is_instr) {
            printf("FETCH WAS INSTR\n");
            preg_F_D.is_instr = true;
        }
        else {
            printf("FETCH WASNT INSTR\n");
            fetch_is_instr = true;
            preg_F_D.is_instr = false;
        }
        return;
    }
    uint32_t instruction = mem_read_32(pipe.PC);
    preg_F_D.instruction = instruction;
    if (last_instruction) {
        preg_F_D.is_instr = false;
        if(increment_decode_halt) {
            increment_decode_halt = false;
            pipe.PC += 4;
        }
        return;
    }

    printf("[FETCH] PC: 0x%lx | Instr: 0x%08x\n", pipe.PC, instruction);
    preg_F_D.is_instr = true; 

    //predicted PC
    bp_predict(pipe.PC, &preg_F_D.next_PC, &preg_F_D.pred_taken);
    preg_F_D.instr_PC = pipe.PC;
	pipe.PC = preg_F_D.next_PC; 
}


// util helpers

uint64_t sign_extend(uint64_t imm, uint32_t sign_bit) {
    uint64_t mask = 1 << sign_bit;
    if (imm & mask) {
        return imm | ~((1 << (sign_bit + 1)) - 1);
    } else {
        return imm;
    }
}

uint64_t extract_bits(uint32_t instruction, uint32_t start, uint32_t end) {
    uint32_t mask = (1 << (end - start + 1)) - 1;
    uint64_t result = (instruction >> start) & mask;
    return result;
}


// intstruction format helpers

void handle_r(uint32_t instruction) {
    preg_D_X.reg1 = extract_bits(instruction, 5, 9);
    preg_D_X.reg2 = extract_bits(instruction, 16, 20);
	preg_D_X.reg1_val = pipe.REGS[preg_D_X.reg1];
	preg_D_X.reg2_val = pipe.REGS[preg_D_X.reg2];
	preg_D_X.dest = extract_bits(instruction, 0, 4);
}

void handle_i(uint32_t instruction) { 
    preg_D_X.reg1 = extract_bits(instruction, 5, 9);
    preg_D_X.reg1_val = pipe.REGS[preg_D_X.reg1];
    preg_D_X.imm = extract_bits(instruction, 10, 21);
    preg_D_X.dest = extract_bits(instruction, 0, 4);
    preg_D_X.immr = extract_bits(instruction, 16, 21);
    preg_D_X.imms = extract_bits(instruction, 10, 15);
}

void handle_d(uint32_t instruction) {
    if ((preg_D_X.opcode == 0x7C0) || (preg_D_X.opcode == 0x1C0) || (preg_D_X.opcode == 0x3C0)) { // STUR opcodes
        preg_D_X.is_stur = true;
    } 
    else {
        preg_D_X.is_load = true;
    }
    uint64_t dt_offset = extract_bits(instruction, 12, 20);
    dt_offset = sign_extend(dt_offset, 8);
    preg_D_X.reg1 = extract_bits(instruction, 5, 9);
    preg_D_X.dest = extract_bits(instruction, 0, 4);
    preg_D_X.reg1_val = pipe.REGS[preg_D_X.reg1];
    preg_D_X.reg2_val = pipe.REGS[preg_D_X.dest];
    preg_D_X.imm = dt_offset;
}

void handle_b(uint32_t instruction) {
    preg_D_X.is_branch = true;
    uint64_t br_addr = extract_bits(instruction, 0, 25);
    br_addr = sign_extend(br_addr, 25);
    br_addr = preg_F_D.instr_PC + (br_addr << 2);
    preg_D_X.addr = br_addr;
    preg_D_X.rt = extract_bits(instruction, 5, 9);
    preg_D_X.reg2 = preg_D_X.rt;
    preg_D_X.reg2_val = pipe.REGS[preg_D_X.rt];
}

void handle_cb(uint32_t instruction) {
    preg_D_X.is_branch = true;
    uint64_t cond_addr = extract_bits(instruction, 5, 23);
    cond_addr = sign_extend(cond_addr, 18);
    cond_addr = preg_F_D.instr_PC + (cond_addr << 2);
    preg_D_X.addr = cond_addr;
    preg_D_X.rt = extract_bits(instruction, 0, 4);
    preg_D_X.reg2 = preg_D_X.rt;
    preg_D_X.reg2_val = pipe.REGS[preg_D_X.rt];
}

void handle_iw(uint32_t instruction) {
    preg_D_X.dest = extract_bits(instruction, 0, 4);
    preg_D_X.mov_shift = extract_bits(instruction, 21, 22) * 16;
    preg_D_X.imm = extract_bits(instruction, 5, 20);
}