#include "compiler.h"

#include "nasm.h"
#include "insns.h"
#include "seed.h"
#include "tmplt.h"
#include "x86pg.h"

struct X86PGState X86PGState;

void init_x86pgstate(void)
{
    X86PGState.seqMode = false;
    bseqi_init(&X86PGState.bseqi);
    init_text_sec(&X86PGState.text_sec);
    init_data_sec(&X86PGState.data_sec);
    X86PGState.labeli = 0;
    X86PGState.opcode = I_none;
    X86PGState.curr_inst = NULL;
    X86PGState.need_init = false;
    X86PGState.instlist = insnlist_create();
    X86PGState.insertpos = NULL;
    X86PGState.lock_ctrl = false;
    X86PGState.lock_edx = false;
    X86PGState.lock_ebx = false;
    X86PGState.lock_ecx = false;
}

void reset_x86pgstate(void)
{
    bseqi_init(&X86PGState.bseqi);
    init_text_sec(&X86PGState.text_sec);
    init_data_sec(&X86PGState.data_sec);
    X86PGState.labeli = 0;
    X86PGState.opcode = I_none;
    X86PGState.curr_inst = NULL;
    X86PGState.need_init = false;
    X86PGState.insertpos = NULL;
    X86PGState.lock_ctrl = false;
    X86PGState.lock_edx = false;
    X86PGState.lock_ebx = false;
    X86PGState.lock_ecx = false;
}

blk_struct *stat_get_curr_blk(void)
{
    return X86PGState.curr_blk;
}

void stat_set_curr_blk(blk_struct *blk)
{
    X86PGState.curr_blk = blk;
}

int stat_get_labeli(void)
{
    return X86PGState.labeli;
}

void stat_inc_labeli(void)
{
    X86PGState.labeli++;
}

enum opcode stat_get_opcode(void)
{
    return X86PGState.opcode;
}

void stat_set_opcode(enum opcode opcode)
{
    X86PGState.opcode = opcode;
}

bool stat_get_need_init(void)
{
    return X86PGState.need_init;
}

void stat_set_need_init(bool need_init)
{
    X86PGState.need_init = need_init;
}

void stat_lock_ctrl(void)
{
    X86PGState.lock_ctrl = true;
}

void stat_unlock_ctrl(void)
{
    X86PGState.lock_ctrl = false;
}

bool stat_ctrl_locked(void)
{
    return X86PGState.lock_ctrl == true;
}

void stat_lock_edx(void)
{
    X86PGState.lock_edx = true;
}

void stat_unlock_edx(void)
{
    X86PGState.lock_edx = false;
}

bool stat_edx_locked(void)
{
    return X86PGState.lock_edx == true;
}

void stat_lock_ebx(void)
{
    X86PGState.lock_ebx = true;
}

void stat_unlock_ebx(void)
{
    X86PGState.lock_ebx = false;
}

bool stat_ebx_locked(void)
{
    return X86PGState.lock_ebx == true;
}

void stat_lock_ecx(void)
{
    X86PGState.lock_ecx = true;
}

void stat_unlock_ecx(void)
{
    X86PGState.lock_ecx = false;
}

bool stat_ecx_locked(void)
{
    return X86PGState.lock_ecx == true;
}

void stat_insert_insn(insn *inst, enum position pos) {
    switch (pos) {
        case INSERT_AFTER:
            X86PGState.insertpos = insnlist_insert_after(X86PGState.instlist, X86PGState.insertpos, inst);
            break;
        case INSERT_BEFORE:
            X86PGState.insertpos = insnlist_insert_before(X86PGState.instlist, X86PGState.insertpos, inst);
            break;
        case INSERT_TAIL:
            X86PGState.insertpos = insnlist_insert_tail(X86PGState.instlist, inst);
            break;
        default:
            break;
    }
}
