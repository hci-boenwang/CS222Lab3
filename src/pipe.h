/***************************************************************/
/*                                                             */
/*   ARM Instruction Level Simulator                           */
/*                                                             */
/*   CMSC-22200 Computer Architecture                          */
/*   University of Chicago                                     */
/*                                                             */
/***************************************************************/

#ifndef _PIPE_H_
#define _PIPE_H_

#include "shell.h"
#include "stdbool.h"
#include <limits.h>

/* Represents an operation travelling through the pipeline. */
typedef struct Pipe_Op {
	bool MemRead;
    bool MemWrite;
	bool MemtoReg;
	bool RegWrite;
	/* place other information here as necessary */
} Pipe_Op;

typedef struct Pipe_Reg_F_D {
	uint32_t instruction;
	uint64_t instr_PC;
	uint64_t next_PC;
	uint32_t reg1;
	uint32_t reg2;
	bool is_instr;
} Pipe_Reg_F_D;

typedef struct Pipe_Reg_D_X {
	uint32_t opcode;
	uint64_t next_PC;
	int64_t reg1_val;
	int64_t reg2_val;
	uint64_t imm;
	uint32_t dest;
	uint32_t immr;
	uint32_t imms;
	uint64_t addr;
	uint32_t rt;
	uint32_t mov_shift;
	uint32_t reg1;
	uint32_t reg2;
	Pipe_Op op;
	bool valid;
	bool is_instr;
	bool is_stur;
	bool is_branch;
	bool is_halt;
	int FLAG_N;
	int FLAG_Z;
} Pipe_Reg_D_X;

typedef struct Pipe_Reg_X_M {
	uint32_t dest;
	int64_t ALU_result;
	uint32_t mem_size;
	uint32_t opcode;
	int64_t reg2_val;
	Pipe_Op op;
	bool valid;
	bool is_instr;
	bool is_stur;
	bool is_halt;
	int FLAG_N;
	int FLAG_Z;
	bool is_branch;
	bool flag_set;
} Pipe_Reg_X_M;

typedef struct Pipe_Reg_M_W {
	uint32_t dest;
	int64_t ALU_result;
	int64_t mem_read_val;
	Pipe_Op op;
	bool valid;
	bool is_instr;
	bool is_stur;
	bool is_halt;
	int FLAG_N;
	int FLAG_Z;
	bool is_branch;
} Pipe_Reg_M_W;

/* Represents the current state of the pipeline. */
typedef struct Pipe_State {
	/* register file state */
	int64_t REGS[ARM_REGS];
	int FLAG_N;        /* flag N */
	int FLAG_Z;        /* flag Z */

	/* program counter in fetch stage */
	uint64_t PC;

	/* place other information here as necessary */
} Pipe_State;

extern int RUN_BIT;

/* global variable -- pipeline state */
extern Pipe_State pipe;

/* called during simulator startup */
void pipe_init();

/* this function calls the others */
void pipe_cycle();

/* each of these functions implements one stage of the pipeline */
void pipe_stage_fetch();
void pipe_stage_decode();
void pipe_stage_execute();
void pipe_stage_mem();
void pipe_stage_wb();

#endif
