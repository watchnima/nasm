#include "compiler.h"

#include "error.h"
#include "seed.h"
#include "generator.h"
#include "insnlist.h"
#include "ofmt.h"
#include "opflags.h"
#include "x86pg.h"
#include "output_file_wrapper.h"

static const char funcs[] = "\
\n\
_get_pc:\n\
  mov eax,[esp]\n\
  ret\n";

static const char main_label[] = "\
  GLOBAL main\n\
main:\n\n\
  call parse_argv\n";

static const char init_regs[] = "\
  mov eax,0x0\n\
  mov ebx,0x0\n\
  mov ecx,0x0\n\
  mov edx,0x0\n\
  mov esi,0x0\n\
  mov edi,0x0\n\
  push eax\n\
  popf \n\
  fninit \n\
  fldz\n\
  fldz\n\
  fldz\n\
  fldz\n\
  fldz\n\
  fldz\n\
  fldz\n\
  fldz\n\
  fninit\n";

static const char check_macro[] = "\
%macro check 1\n\
  pusha\n\
  call _get_pc\n\
  push eax\n\
  pushf\n\
  push cs\n\
  push ss\n\
  push ds\n\
  push es\n\
  push fs\n\
  push gs\n\
  sub esp,0x6c\n\
  fsave [esp]\n\
  movupd [fxstate],xmm0\n\
  movupd [fxstate + 0x10],xmm1\n\
  movupd [fxstate + 0x20],xmm2\n\
  movupd [fxstate + 0x30],xmm3\n\
  movupd [fxstate + 0x40],xmm4\n\
  movupd [fxstate + 0x50],xmm5\n\
  movupd [fxstate + 0x60],xmm6\n\
  movupd [fxstate + 0x70],xmm7\n\
  stmxcsr [fxstate + 0x80]\n\
  lea eax,fxstate\n\
  push eax\n\
  call check_point_%1\n\
  pop eax\n\
  frstor [esp]\n\
  add esp,0x6c\n\
  pop eax\n\
  pop eax\n\
  pop eax\n\
  pop eax\n\
  pop eax\n\
  pop eax\n\
  popf\n\
  pop eax\n\
  popa\n\
%endmacro\n";

static const char safe_exit[] = "\n\
  call check_point_end\n\
  mov eax,1\n\
  mov ebx,0\n\
  int 80h";

static char *check_function_names[] =
{
#define DEFINE_CHECK_FUNCTION(name,type,func) #func,
#include "./user-lib/checkfunctions.h"
    "check_point_end",
    "parse_argv"
#undef DEFINE_CHECK_FUNCTION
};

void open_output_file(const char *fname)
{
    struct output_data data;

    ofmt->init(fname);

    data.type = OUTPUT_EXTERN;
    for (size_t i = 0; i < ARRAY_SIZE(check_function_names); i++) {
        data.buf = (const void *)check_function_names[i];
        ofmt->output(&data);
    }

    data.type = OUTPUT_RAWDATA;
    data.buf = "";
    ofmt->output(&data);
    data.buf = (const void *)check_macro;
    ofmt->output(&data);

    data.type = OUTPUT_SECTION;
    data.buf = (const void *)&X86PGState.data_sec;
    ofmt->output(&data);

    data.type = OUTPUT_SECTION;
    data.buf = (const void *)&X86PGState.text_sec;
    ofmt->output(&data);

    data.type = OUTPUT_RAWDATA;
    data.buf = (const void *)funcs;
    ofmt->output(&data);
    data.buf = (const void *)main_label;
    ofmt->output(&data);
    data.buf = (const void *)init_regs;
    ofmt->output(&data);

    reset_x86pgstate();
}

void close_output_file(void)
{
    end_insn_gen();

    insnlist_output(X86PGState.instlist, ofmt);
    insnlist_clear(X86PGState.instlist);

    struct output_data data;
    data.type = OUTPUT_RAWDATA;
    data.buf = (const void *)safe_exit;
    ofmt->output(&data);

    ofmt->cleanup();
}
