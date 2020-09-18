/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2020 The NASM Authors - All Rights Reserved
 *   See the file AUTHORS included with the NASM distribution for
 *   the specific copyright holders.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following
 *   conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *     CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *     INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *     MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *     DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 *     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *     SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *     NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *     OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *     EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ----------------------------------------------------------------------- */

/*
 * The Netwide Assembler main program module
 */

#include "compiler.h"


#include "nasm.h"
#include "assemble.h"
#include "listing.h"
#include "insns.h"
#include "nctype.h"
#include "generator.h"
#include "parser.h"
#include "gendata.h"

/*
 * This is the maximum number of optimization passes to do.  If we ever
 * find a case where the optimizer doesn't naturally converge, we might
 * have to drop this value so the assembler doesn't appear to just hang.
 */
struct forwrefinfo {            /* info held on forward refs. */
    int lineno;
    int operand;
};

static bool skip_this_pass(errflags severity);

struct error_format {
    const char *beforeline;     /* Before line number, if present */
    const char *afterline;      /* After line number, if present */
    const char *beforemsg;      /* Before actual message */
};

static const struct error_format errfmt_gnu  = { ":", "",  ": "  };
static const struct error_format *errfmt = &errfmt_gnu;
static struct strlist *warn_list;
static struct nasm_errhold *errhold_stack;

static bool opt_verbose_info;

#ifndef ABORT_ON_PANIC
# define ABORT_ON_PANIC 0
#endif
static bool abort_on_panic = ABORT_ON_PANIC;
static bool keep_all;

int globalrel = 0;
int globalbnd = 0;

static const char *listname = "list.tmp";

extern FILE *error_file;               /* Where to write error messages */

static int cmd_sb = 32;    /* by default */

iflag_t cpu;
static iflag_t cmd_cpu;

struct location location;
bool in_absolute;                 /* Flag we are in ABSOLUTE seg */
struct location absolute;         /* Segment/offset inside ABSOLUTE */

int64_t switch_segment(int32_t segment)
{
    location.segment = segment;
    if (segment == NO_SEG) {
        location.offset = absolute.offset;
        in_absolute = true;
    } else {
        in_absolute = false;
    }
    return location.offset;
}

static void set_curr_offs(int64_t l_off)
{
        if (in_absolute)
            absolute.offset = l_off;
}

static void increment_offset(int64_t delta)
{
    if (unlikely(delta == 0))
        return;

    location.offset += delta;
    set_curr_offs(location.offset);
}

void generator_init(void)
{
  error_file = stderr;

  iflag_set_default_cpu(&cpu);
  iflag_set_default_cpu(&cmd_cpu);

  /* Save away the default state of warnings */
  init_warnings();

  gendata_init();

  nasm_ctype_init();
}

void generator_exit(void)
{
  src_free();
}

static void process_insn(insn *instruction)
{
    int32_t n;
    int64_t l;

    if (!instruction->times)
        return;                 /* Nothing to do... */

    nasm_assert(instruction->times > 0);

    /*
     * NOTE: insn_size() can change instruction->times
     * (usually to 1) when called.
     */
    if (!pass_final()) {
        int64_t start = location.offset;
        for (n = 1; n <= instruction->times; n++) {
            l = insn_size(location.segment, location.offset,
                          globalbits, instruction);
            /* l == -1 -> invalid instruction */
            if (l != -1)
                increment_offset(l);
        }
        if (list_option('p')) {
            struct out_data dummy;
            memset(&dummy, 0, sizeof dummy);
            dummy.type   = OUT_RAWDATA; /* Handled specially with .data NULL */
            dummy.offset = start;
            dummy.size   = location.offset - start;
            lfmt->output(&dummy);
        }
    } else {
        l = assemble(location.segment, location.offset,
                     globalbits, instruction);
                /* We can't get an invalid instruction here */
        increment_offset(l);

        if (instruction->times > 1) {
            lfmt->uplevel(LIST_TIMES, instruction->times);
            for (n = 2; n <= instruction->times; n++) {
                l = assemble(location.segment, location.offset,
                             globalbits, instruction);
                increment_offset(l);
            }
            lfmt->downlevel(LIST_TIMES);
        }
    }
}

void generate(insn_seed *seed, insn *output_ins)
{
    uint64_t prev_offset_changed;
    int64_t stall_count = 0; /* Make sure we make forward progress... */

    switch (cmd_sb) {
    case 16:
        break;
    case 32:
        if (!iflag_cpu_level_ok(&cmd_cpu, IF_386))
            nasm_fatal("command line: 32-bit segment size requires a higher cpu");
        break;
    case 64:
        if (!iflag_cpu_level_ok(&cmd_cpu, IF_X86_64))
            nasm_fatal("command line: 64-bit segment size requires a higher cpu");
        break;
    default:
        panic();
        break;
    }

    prev_offset_changed = INT64_MAX;

    if (listname && !keep_all) {
        /* Remove the list file in case we die before the output pass */
        remove(listname);
    }

        global_offset_changed = 0;

	/*
	 * Create a warning buffer list unless we are in
         * pass 2 (everything will be emitted immediately in pass 2.)
	 */
	if (warn_list) {
            if (warn_list->nstr || pass_final())
                strlist_free(&warn_list);
        }

	if (!pass_final() && !warn_list)
            warn_list = strlist_alloc(false);

        globalbits = cmd_sb;  /* set 'bits' to command line default */
        cpu = cmd_cpu;
        if (listname) {
            if (pass_final() || list_on_every_pass()) {
                lfmt->init(listname);
            } else if (list_active()) {
                /*
                 * Looks like we used the list engine on a previous pass,
                 * but now it is turned off, presumably via %pragma -p
                 */
                lfmt->cleanup();
                if (!keep_all)
                    remove(listname);
            }
        }

        in_absolute = false;
        location.segment = NO_SEG;
        location.offset  = 0;

        /* Not a directive, or even something that starts with [ */
        parse_insn_seed(seed, output_ins);
        process_insn(output_ins);

        /* We better not be having an error hold still... */
        nasm_assert(!errhold_stack);

        if (global_offset_changed) {
            switch (pass_type()) {
            case PASS_OPT:
                /*
                 * This is the only pass type that can be executed more
                 * than once, and therefore has the ability to stall.
                 */
                if (global_offset_changed < prev_offset_changed) {
                    prev_offset_changed = global_offset_changed;
                    stall_count = 0;
                } else {
                    stall_count++;
                }

                break;

            case PASS_STAB:
                /*!
                 *!phase [off] phase error during stabilization
                 *!  warns about symbols having changed values during
                 *!  the second-to-last assembly pass. This is not
                 *!  inherently fatal, but may be a source of bugs.
                 */
                nasm_warn(WARN_PHASE|ERR_UNDEAD,
                          "phase error during stabilization "
                          "pass, hoping for the best");
                break;

            case PASS_FINAL:
                nasm_nonfatalf(ERR_UNDEAD,
                               "phase error during code generation pass");
                break;

            default:
                /* This is normal, we'll keep going... */
                break;
            }
        }

        reset_warnings();

    if (opt_verbose_info && pass_final()) {
        /*  -On and -Ov switches */
        nasm_info("assembly required 1+%"PRId64"+2 passes\n", pass_count()-3);
    }

    lfmt->cleanup();
    strlist_free(&warn_list);
}

/**
 * get warning index; 0 if this is non-suppressible.
 */
static size_t warn_index(errflags severity)
{
    size_t index;

    if ((severity & ERR_MASK) >= ERR_FATAL)
        return 0;               /* Fatal errors are never suppressible */

    /* Warnings MUST HAVE a warning category specifier! */
    nasm_assert((severity & (ERR_MASK|WARN_MASK)) != ERR_WARNING);

    index = WARN_IDX(severity);
    nasm_assert(index < WARN_IDX_ALL);

    return index;
}

static bool skip_this_pass(errflags severity)
{
    errflags type = severity & ERR_MASK;

    /*
     * See if it's a pass-specific error or warning which should be skipped.
     * We can never skip fatal errors as by definition they cannot be
     * resumed from.
     */
    if (type >= ERR_FATAL)
        return false;

    /*
     * ERR_LISTMSG messages are always skipped; the list file
     * receives them anyway as this function is not consulted
     * for sending to the list file.
     */
    if (type == ERR_LISTMSG)
        return true;

    /*
     * This message not applicable unless it is the last pass we are going
     * to execute; this can be either the final code-generation pass or
     * the single pass executed in preproc-only mode.
     */
    return (severity & ERR_PASS2) && !pass_final_or_preproc();
}

/**
 * check for suppressed message (usually warnings or notes)
 *
 * @param severity the severity of the warning or error
 * @return true if we should abort error/warning printing
 */
static bool is_suppressed(errflags severity)
{
    /* Fatal errors must never be suppressed */
    if ((severity & ERR_MASK) >= ERR_FATAL)
        return false;

    if (!(warning_state[warn_index(severity)] & WARN_ST_ENABLED))
        return true;

    return false;
}

/**
 * Return the true error type (the ERR_MASK part) of the given
 * severity, accounting for warnings that may need to be promoted to
 * error.
 *
 * @param severity the severity of the warning or error
 * @return true if we should error out
 */
static errflags true_error_type(errflags severity)
{
    const uint8_t warn_is_err = WARN_ST_ENABLED|WARN_ST_ERROR;
    int type;

    type = severity & ERR_MASK;

    /* Promote warning to error? */
    if (type == ERR_WARNING) {
        uint8_t state = warning_state[warn_index(severity)];
        if ((state & warn_is_err) == warn_is_err)
            type = ERR_NONFATAL;
    }

    return type;
}

/*
 * The various error type prefixes
 */
static const char * const error_pfx_table[ERR_MASK+1] = {
    ";;; ", "debug: ", "info: ", "warning: ",
        "error: ", "fatal: ", "critical: ", "panic: "
};
static const char no_file_name[] = "nasm"; /* What to print if no file name */

/*
 * For fatal/critical/panic errors, kill this process.
 */
static fatal_func die_hard(errflags true_type)
{
    fflush(NULL);

    if (true_type == ERR_PANIC && abort_on_panic)
        abort();

    /* Terminate immediately */
    exit(true_type - ERR_FATAL + 1);
}

/*
 * Returns the struct src_location appropriate for use, after some
 * potential filename mangling.
 */
static struct src_location error_where(errflags severity)
{
    struct src_location where;

    if (severity & ERR_NOFILE) {
        where.filename = NULL;
        where.lineno = 0;
    } else {
        where = src_where_error();

        if (!where.filename) {
            where.filename = NULL;
            where.lineno = 0;
        }
    }

    return where;
}

/*
 * error reporting for critical and panic errors: minimize
 * the amount of system dependencies for getting a message out,
 * and in particular try to avoid memory allocations.
 */
fatal_func nasm_verror_critical(errflags severity, const char *fmt, va_list args)
{
    struct src_location where;
    errflags true_type = severity & ERR_MASK;
    static bool been_here = false;

    if (unlikely(been_here))
        abort();                /* Recursive error... just die */

    been_here = true;

    where = error_where(severity);
    if (!where.filename)
        where.filename = no_file_name;

    fputs(error_pfx_table[severity], error_file);
    fputs(where.filename, error_file);
    if (where.lineno) {
        fprintf(error_file, "%s%"PRId32"%s",
                errfmt->beforeline, where.lineno, errfmt->afterline);
    }
    fputs(errfmt->beforemsg, error_file);
    vfprintf(error_file, fmt, args);
    fputc('\n', error_file);

    die_hard(true_type);
}

/**
 * Stack of tentative error hold lists.
 */
struct nasm_errtext {
    struct nasm_errtext *next;
    char *msg;                  /* Owned by this structure */
    struct src_location where;  /* Owned by the srcfile system */
    errflags severity;
    errflags true_type;
};
struct nasm_errhold {
    struct nasm_errhold *up;
    struct nasm_errtext *head, **tail;
};

static void nasm_free_error(struct nasm_errtext *et)
{
    nasm_free(et->msg);
    nasm_free(et);
}

static void nasm_issue_error(struct nasm_errtext *et);

struct nasm_errhold *nasm_error_hold_push(void)
{
    struct nasm_errhold *eh;

    nasm_new(eh);
    eh->up = errhold_stack;
    eh->tail = &eh->head;
    errhold_stack = eh;

    return eh;
}

void nasm_error_hold_pop(struct nasm_errhold *eh, bool issue)
{
    struct nasm_errtext *et, *etmp;

    /* Allow calling with a null argument saying no hold in the first place */
    if (!eh)
        return;

    /* This *must* be the current top of the errhold stack */
    nasm_assert(eh == errhold_stack);

    if (eh->head) {
        if (issue) {
            if (eh->up) {
                /* Commit the current hold list to the previous level */
                *eh->up->tail = eh->head;
                eh->up->tail = eh->tail;
            } else {
                /* Issue errors */
                list_for_each_safe(et, etmp, eh->head)
                    nasm_issue_error(et);
            }
        } else {
            /* Free the list, drop errors */
            list_for_each_safe(et, etmp, eh->head)
                nasm_free_error(et);
        }
    }

    errhold_stack = eh->up;
    nasm_free(eh);
}

/**
 * common error reporting
 * This is the common back end of the error reporting schemes currently
 * implemented.  It prints the nature of the warning and then the
 * specific error message to error_file and may or may not return.  It
 * doesn't return if the error severity is a "panic" or "debug" type.
 *
 * @param severity the severity of the warning or error
 * @param fmt the printf style format string
 */
void nasm_verror(errflags severity, const char *fmt, va_list args)
{
    struct nasm_errtext *et;
    errflags true_type = true_error_type(severity);

    if (true_type >= ERR_CRITICAL)
        nasm_verror_critical(severity, fmt, args);

    if (is_suppressed(severity))
        return;

    nasm_new(et);
    et->severity = severity;
    et->true_type = true_type;
    et->msg = nasm_vasprintf(fmt, args);
    et->where = error_where(severity);

    if (errhold_stack && true_type <= ERR_NONFATAL) {
        /* It is a tentative error */
        *errhold_stack->tail = et;
        errhold_stack->tail = &et->next;
    } else {
        nasm_issue_error(et);
    }

    /*
     * Don't do this before then, if we do, we lose messages in the list
     * file, as the list file is only generated in the last pass.
     */
    if (skip_this_pass(severity))
        return;
}

/*
 * Actually print, list and take action on an error
 */
static void nasm_issue_error(struct nasm_errtext *et)
{
    const char *pfx;
    char warnsuf[64];           /* Warning suffix */
    char linestr[64];           /* Formatted line number if applicable */
    const errflags severity  = et->severity;
    const errflags true_type = et->true_type;
    const struct src_location where = et->where;

    if (severity & ERR_NO_SEVERITY)
        pfx = "";
    else
        pfx = error_pfx_table[true_type];

    *warnsuf = 0;
    if ((severity & (ERR_MASK|ERR_HERE|ERR_PP_LISTMACRO)) == ERR_WARNING) {
        /*
         * It's a warning without ERR_HERE defined, and we are not already
         * unwinding the macros that led us here.
         */
        snprintf(warnsuf, sizeof warnsuf, " [-w+%s%s]",
                 (true_type >= ERR_NONFATAL) ? "error=" : "",
                 warning_name[warn_index(severity)]);
    }

    *linestr = 0;
    if (where.lineno) {
        snprintf(linestr, sizeof linestr, "%s%"PRId32"%s",
                 errfmt->beforeline, where.lineno, errfmt->afterline);
    }

    if (!skip_this_pass(severity)) {
        const char *file = where.filename ? where.filename : no_file_name;
        const char *here = "";

        if (severity & ERR_HERE) {
            here = where.filename ? " here" : " in an unknown location";
        }

        if (warn_list && true_type < ERR_NONFATAL) {
            /*
             * Buffer up warnings until we either get an error
             * or we are on the code-generation pass.
             */
            strlist_printf(warn_list, "%s%s%s%s%s%s%s",
                           file, linestr, errfmt->beforemsg,
                           pfx, et->msg, here, warnsuf);
        } else {
            /*
             * Actually output an error.  If we have buffered
             * warnings, and this is a non-warning, output them now.
             */
            if (true_type >= ERR_NONFATAL && warn_list) {
                strlist_write(warn_list, "\n", error_file);
                strlist_free(&warn_list);
            }

            fprintf(error_file, "%s%s%s%s%s%s%s\n",
                    file, linestr, errfmt->beforemsg,
                    pfx, et->msg, here, warnsuf);
        }
    }

    /* Are we recursing from error_list_macros? */
    if (severity & ERR_PP_LISTMACRO)
        goto done;

    /*
     * Don't suppress this with skip_this_pass(), or we don't get
     * pass1 or preprocessor warnings in the list file
     */
    if (severity & ERR_HERE) {
        if (where.lineno)
            lfmt->error(severity, "%s%s at %s:%"PRId32"%s",
                        pfx, et->msg, where.filename, where.lineno, warnsuf);
        else if (where.filename)
            lfmt->error(severity, "%s%s in file %s%s",
                        pfx, et->msg, where.filename, warnsuf);
        else
            lfmt->error(severity, "%s%s in an unknown location%s",
                        pfx, et->msg, warnsuf);
    } else {
        lfmt->error(severity, "%s%s%s", pfx, et->msg, warnsuf);
    }

    if (skip_this_pass(severity))
        goto done;

    if (true_type >= ERR_FATAL)
        die_hard(true_type);

done:
    nasm_free_error(et);
}