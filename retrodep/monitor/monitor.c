/*
 * monitor.c - The VICE built-in monitor.
 *
 * Written by
 *  Daniel Sladic <sladic@eecg.toronto.edu>
 *  Ettore Perazzoli <ettore@comm2000.it>
 *  Andreas Boose <viceteam@t-online.de>
 *  Daniel Kahlin <daniel@kahlin.net>
 *  Thomas Giesel <skoe@directbox.com>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __IBMC__
#include <direct.h>
#endif

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#include "archdep.h"
#include "charset.h"
#include "cmdline.h"
#include "console.h"
#include "datasette.h"
#include "drive.h"

#ifdef HAVE_FULLSCREEN
#include "fullscreenarch.h"
#endif

#if 0
#include "interrupt.h"
#include "ioutil.h"
#include "kbdbuf.h"
#include "lib.h"
#include "log.h"
#include "machine.h"
#include "machine-video.h"
#endif
#include "mem.h"
#include "mon_breakpoint.h"
#include "mon_disassemble.h"
#include "mon_memmap.h"
#include "mon_memory.h"
#include "asm.h"

#ifdef AMIGA_MORPHOS
#undef REG_PC
#endif

#include "mon_parse.h"
#include "mon_register.h"
#if 0
#include "mon_ui.h"
#include "mon_util.h"
#include "monitor.h"
#include "monitor_binary.h"
#include "monitor_network.h"
#include "montypes.h"
#include "resources.h"
#include "screenshot.h"
#include "sysfile.h"
#include "traps.h"
#include "types.h"
#endif
#include "uiapi.h"
#if 0
#include "uimon.h"
#include "util.h"
#include "vsync.h"

#ifndef HAVE_STPCPY
char *stpcpy(char *dest, const char *src)
{
    char *d = dest;
    const char *s = src;

    do {
        *d++ = *s;
    } while (*s++ != '\0');

    return d - 1;
}
#endif
#endif
int mon_stop_output;

int mon_init_break = -1;

/* Defines */
#if 0
#define MAX_LABEL_LEN 255
#define MAX_MEMSPACE_NAME_LEN 10
#define HASH_ARRAY_SIZE 256
#define HASH_ADDR(x) ((x) % 0xff)
#define OP_JSR 0x20
#define OP_RTI 0x40
#define OP_RTS 0x60

#define ADDR_LIMIT(x) (addr_mask(x))
#define BAD_ADDR (new_addr(e_invalid_space, 0))

#define MONITOR_GET_PC(mem) \
    ((uint16_t)((monitor_cpu_for_memspace[mem]->mon_register_get_val)(mem, e_PC)))

#define MONITOR_GET_OPCODE(mem) (mon_get_mem_val(mem, MONITOR_GET_PC(mem)))

console_t *console_log = NULL;

static int monitor_trap_triggered = 0;
#endif
monitor_cartridge_commands_t mon_cart_cmd;

/* Types */
#if 0
struct symbol_entry {
    uint16_t addr;
    char *name;
    struct symbol_entry *next;
};
typedef struct symbol_entry symbol_entry_t;

struct symbol_table {
    symbol_entry_t *name_list;
    symbol_entry_t *addr_hash_table[HASH_ARRAY_SIZE];
};
typedef struct symbol_table symbol_table_t;
#endif
/* Global variables */

/* Defined in file generated by bison. Set to 1 to get
 * parsing debug information. */
extern int yydebug;
#if 0
static char *last_cmd = NULL;
int exit_mon = 0;
/* flag used by the single-stepping commands to prevent switching forth and back
 * to the emulator window when stepping a single instruction. note that this is
 * different from what keep_monitor_open does :)
 */
static int mon_console_suspend_on_leaving = 1;
/* flag used by the eXit command. it will force the monitor to close regardless
 * of keep_monitor_open.
 */
static int mon_console_close_on_leaving = 0;
#endif
int sidefx;
RADIXTYPE default_radix;
MEMSPACE default_memspace;
#if 0
static bool inside_monitor = FALSE;
static unsigned int instruction_count;
static bool skip_jsrs;
static int wait_for_return_level;

static uint16_t watch_load_array[10][NUM_MEMSPACES];
static uint16_t watch_store_array[10][NUM_MEMSPACES];
static unsigned int watch_load_count[NUM_MEMSPACES];
static unsigned int watch_store_count[NUM_MEMSPACES];
static symbol_table_t monitor_labels[NUM_MEMSPACES];
static CLOCK stopwatch_start_time[NUM_MEMSPACES];

bool force_array[NUM_MEMSPACES];
monitor_interface_t *mon_interfaces[NUM_MEMSPACES];

MON_ADDR dot_addr[NUM_MEMSPACES];
unsigned char data_buf[256];
unsigned char data_mask_buf[256];
unsigned int data_buf_len;
bool asm_mode;
MON_ADDR asm_mode_addr;

static unsigned int next_or_step_stop;
#endif
unsigned monitor_mask[NUM_MEMSPACES];
#if 0
static bool watch_load_occurred;
static bool watch_store_occurred;

static bool recording;
static FILE *recording_fp;
static char *recording_name;
#define MAX_PLAYBACK 8
int playback = 0;
char *playback_name = NULL;
static void playback_commands(int current_playback);
static int set_playback_name(const char *param, void *extra_param);
#endif
/* Disassemble the current opcode on entry.  Used for single step.  */
#if 0
static int disassemble_on_entry = 0;

/* We now have an array of pointers to the current monitor_cpu_type for each memspace. */
/* This gets initialized in monitor_init(). */
monitor_cpu_type_t *monitor_cpu_for_memspace[NUM_MEMSPACES];

struct supported_cpu_type_list_s {
    monitor_cpu_type_t *monitor_cpu_type_p;
    struct supported_cpu_type_list_s *next;
};
typedef struct supported_cpu_type_list_s supported_cpu_type_list_t;

/* A linked list of supported monitor_cpu_types for each memspace */
static supported_cpu_type_list_t *monitor_cpu_type_supported[NUM_MEMSPACES];

struct monitor_cpu_type_list_s {
    monitor_cpu_type_t monitor_cpu_type;
    struct monitor_cpu_type_list_s *next_monitor_cpu_type;
};
typedef struct monitor_cpu_type_list_s monitor_cpu_type_list_t;

static monitor_cpu_type_list_t *monitor_cpu_type_list = NULL;

/* Some local helper functions */
static int find_cpu_type_from_string(const char *cpu_string)
{
    return -1;
}

static monitor_cpu_type_t* monitor_find_cpu_for_memspace(MEMSPACE mem, CPU_TYPE_t cpu)
{
    return NULL;
}

static void monitor_print_cpu_types_supported(MEMSPACE mem)
{
}

/* *** ADDRESS FUNCTIONS *** */


static void set_addr_memspace(MON_ADDR *a, MEMSPACE m)
{
    *a = new_addr(m, addr_location(*a));
}
#endif
bool mon_is_valid_addr(MON_ADDR a)
{
    return addr_memspace(a) != e_invalid_space;
}

bool mon_inc_addr_location(MON_ADDR *a, unsigned inc)
{
    unsigned new_loc = addr_location(*a) + inc;
    *a = new_addr(addr_memspace(*a), addr_mask(new_loc));

    return !(new_loc == addr_location(new_loc));
}

void mon_evaluate_default_addr(MON_ADDR *a)
{
}

bool mon_is_in_range(MON_ADDR start_addr, MON_ADDR end_addr, unsigned loc)
{
    return FALSE;
}
#if 0
static bool is_valid_addr_range(MON_ADDR start_addr, MON_ADDR end_addr)
{
    return TRUE;
}

static unsigned get_range_len(MON_ADDR addr1, MON_ADDR addr2)
{
    return 0;
}
#endif
long mon_evaluate_address_range(MON_ADDR *start_addr, MON_ADDR *end_addr,
                                bool must_be_range, uint16_t default_len)
{
    long len = default_len;
    return len;
}


/* *** REGISTER AND MEMORY OPERATIONS *** */
#if 0
mon_reg_list_t *mon_register_list_get(int mem)
{
    return NULL;
}

bool check_drive_emu_level_ok(int drive_num)
{
    return TRUE;
}

void monitor_cpu_type_set(const char *cpu_type)
{
}

int mon_banknum_from_bank(MEMSPACE mem, const char *bankname)
{
    return 0;
}

void mon_bank(MEMSPACE mem, const char *bankname)
{
}

const char *mon_get_bank_name_for_bank(MEMSPACE mem, int banknum)
{
    return NULL;
}

const char *mon_get_current_bank_name(MEMSPACE mem)
{
    return mon_get_bank_name_for_bank(mem, mon_interfaces[mem]->current_bank);
}

/*
    main entry point for the monitor to read a value from memory

    mem_bank_peek and mem_bank_read are set up in src/drive/drivecpu.c,
    src/mainc64cpu.c:358, src/mainviccpu.c:237, src/maincpu.c:296
*/

uint8_t mon_get_mem_val_ex(MEMSPACE mem, int bank, uint16_t mem_addr)
{
    return 0;
}

uint8_t mon_get_mem_val(MEMSPACE mem, uint16_t mem_addr)
{
    return mon_get_mem_val_ex(mem, mon_interfaces[mem]->current_bank, mem_addr);
}

void mon_get_mem_block_ex(MEMSPACE mem, int bank, uint16_t start, uint16_t end, uint8_t *data)
{
}

void mon_get_mem_block(MEMSPACE mem, uint16_t start, uint16_t end, uint8_t *data)
{
}

void mon_set_mem_val(MEMSPACE mem, uint16_t mem_addr, uint8_t val)
{
}

/* exit monitor  */
void mon_jump(MON_ADDR addr)
{
}

/* exit monitor  */
void mon_go(void)
{
}

/* exit monitor, close monitor window  */
void mon_exit(void)
{
}

/* If we want 'quit' for OS/2 I couldn't leave the emulator by calling exit(0)
   So I decided to skip this (I think it's unnecessary for OS/2 */
void mon_quit(void)
{
}

void mon_keyboard_feed(const char *string)
{
}
#endif
/* *** ULTILITY FUNCTIONS *** */
#if 0
void mon_print_bin(int val, char on, char off)
{
}

static void print_hex(int val)
{
}

static void print_octal(int val)
{
}


void mon_print_convert(int val)
{
}

void mon_clear_buffer(void)
{
}

void mon_add_number_to_buffer(int number)
{
}

void mon_add_number_masked_to_buffer(int number, int mask)
{
}

void mon_add_string_to_buffer(char *str)
{
}

static monitor_cpu_type_list_t *monitor_list_new(void)
{
    return NULL;
}

static void montor_list_destroy(monitor_cpu_type_list_t *list)
{
    lib_free(list);
}
#endif
void mon_backtrace(void)
{
}

void mon_screenshot_save(const char* filename, int format)
{
}

void mon_show_pwd(void)
{
}

void mon_show_dir(const char *path)
{
}

void mon_resource_get(const char *name)
{
}

void mon_resource_set(const char *name, const char* value)
{
}

void mon_reset_machine(int type)
{
}

void mon_tape_ctrl(int command)
{
}

void mon_cart_freeze(void)
{
}

void mon_export(void)
{
}

void mon_stopwatch_show(const char* prefix, const char* suffix)
{
}

void mon_stopwatch_reset(void)
{
}

/* Local helper functions for building the lists */
#if 0
static monitor_cpu_type_t* find_monitor_cpu_type(CPU_TYPE_t cputype)
{
    return NULL;
}

static void add_monitor_cpu_type_supported(supported_cpu_type_list_t **list_ptr, monitor_cpu_type_t *mon_cpu_type)
{
}

static void find_supported_monitor_cpu_types(supported_cpu_type_list_t **list_ptr, monitor_interface_t *mon_interface)
{
}
#endif
/* *** MISC COMMANDS *** */
#if 0
monitor_cpu_type_t* monitor_find_cpu_type_from_string(const char *cpu_type)
{
    return NULL;
}
#endif
void monitor_init(monitor_interface_t *maincpu_interface_init,
                  monitor_interface_t *drive_interface_init[],
                  monitor_cpu_type_t **asmarray)
{
}

void monitor_shutdown(void)
{
}

#if 0
static int monitor_set_initial_breakpoint(const char *param, void *extra_param)
{
    return 0;
}

static int keep_monitor_open = 0;

#ifdef ARCHDEP_SEPERATE_MONITOR_WINDOW
static int set_keep_monitor_open(int val, void *param)
{
    return 0;
}
#endif

static const resource_int_t resources_int[] = {
#ifdef ARCHDEP_SEPERATE_MONITOR_WINDOW
    { "KeepMonitorOpen", 1, RES_EVENT_NO, NULL,
      &keep_monitor_open, set_keep_monitor_open, NULL },
#endif
    RESOURCE_INT_LIST_END
};
#endif
int monitor_resources_init(void)
{
    return 0;
}
#if 0
/* FIXME: we should use a resource like MonitorLogFileName */
static int set_monlog_name(const char *param, void *extra_param)
{
    return mon_log_file_open(param);
}
#endif
static const cmdline_option_t cmdline_options[] =
{
#if 0
    { "-moncommands", CALL_FUNCTION, CMDLINE_ATTRIB_NEED_ARGS,
      set_playback_name, NULL, NULL, NULL,
      "<Name>", "Execute monitor commands from file" },
    { "-monlog", CALL_FUNCTION, CMDLINE_ATTRIB_NEED_ARGS,
      set_monlog_name, NULL, NULL, NULL,
      "<Name>", "Write monitor output also to file" },
    { "-initbreak", CALL_FUNCTION, CMDLINE_ATTRIB_NEED_ARGS,
      monitor_set_initial_breakpoint, NULL, NULL, NULL,
      "<value>", "Set an initial breakpoint for the monitor" },
#ifdef ARCHDEP_SEPERATE_MONITOR_WINDOW
    { "-keepmonopen", SET_RESOURCE, CMDLINE_ATTRIB_NONE,
      NULL, NULL, "KeepMonitorOpen", (resource_value_t)1,
      NULL, "Keep the monitor open" },
    { "+keepmonopen", SET_RESOURCE, CMDLINE_ATTRIB_NONE,
      NULL, NULL, "KeepMonitorOpen", (resource_value_t)0,
      NULL, "Do not keep the monitor open" },
#endif
#endif
    CMDLINE_LIST_END
};

int monitor_cmdline_options_init(void)
{
    mon_cart_cmd.cartridge_attach_image = NULL;
    mon_cart_cmd.cartridge_detach_image = NULL;
    mon_cart_cmd.cartridge_trigger_freeze = NULL;
    mon_cart_cmd.cartridge_trigger_freeze_nmi_only = NULL;

    return cmdline_register_options(cmdline_options);
}

monitor_interface_t *monitor_interface_new(void)
{
    return (monitor_interface_t *)lib_calloc(sizeof(monitor_interface_t), 1);
}

void monitor_interface_destroy(monitor_interface_t *monitor_interface)
{
    lib_free(monitor_interface);
}

void mon_start_assemble_mode(MON_ADDR addr, char *asm_line)
{
}

/* ------------------------------------------------------------------------- */

/* Memory.  */

void mon_display_screen(long addr)
{
}

/*
    display io regs

    if addr = 0 display full list, no details
    if addr = 1 display full list, with details

    for other addr display full details for respective device
*/
void mon_display_io_regs(MON_ADDR addr)
{
}

void mon_ioreg_add_list(struct mem_ioreg_list_s **list, const char *name,
                               int start, int end, void *dump, void *context, int mirror_mode)
{
}

#if 0
/* *** FILE COMMANDS *** */

void mon_change_dir(const char *path)
{
}

void mon_save_symbols(MEMSPACE mem, const char *filename)
{
}


/* *** COMMAND FILES *** */


void mon_record_commands(char *filename)
{
}

void mon_end_recording(void)
{
}

static int set_playback_name(const char *param, void *extra_param)
{
    return 0;
}

static void playback_commands(int current_playback)
{
}

void mon_playback_init(const char *filename)
{
}
#endif

/* *** SYMBOL TABLE *** */

#if 0
static void free_symbol_table(MEMSPACE mem)
{
}

char *mon_symbol_table_lookup_name(MEMSPACE mem, uint16_t addr)
{
    return NULL;
}

/* look up a symbol in the given memspace, returns address or -1 on error */
int mon_symbol_table_lookup_addr(MEMSPACE mem, char *name)
{
    return -1;
}

char* mon_prepend_dot_to_name(char* name)
{
    return NULL;
}

void mon_add_name_to_symbol_table(MON_ADDR addr, char *name)
{
}

void mon_remove_name_from_symbol_table(MEMSPACE mem, char *name)
{
}

void mon_print_symbol_table(MEMSPACE mem)
{
}

void mon_clear_symbol_table(MEMSPACE mem)
{
}


/* *** INSTRUCTION COMMANDS *** */


void mon_instructions_step(int count)
{
}

void mon_instructions_next(int count)
{
}

void mon_instruction_return(void)
{
}

void mon_stack_up(int count)
{
}

void mon_stack_down(int count)
{
}


/* *** CONDITIONAL EXPRESSIONS *** */


void mon_print_conditional(cond_node_t *cnode)
{
}


int mon_evaluate_conditional(cond_node_t *cnode)
{
    return 0;
}


void mon_delete_conditional(cond_node_t *cnode)
{
}
#endif

/* *** WATCHPOINTS *** */


void monitor_watch_push_load_addr(uint16_t addr, MEMSPACE mem)
{
}

void monitor_watch_push_store_addr(uint16_t addr, MEMSPACE mem)
{
}
#if 0
static bool watchpoints_check_loads(MEMSPACE mem, unsigned int lastpc, unsigned int pc)
{
    return FALSE;
}

static bool watchpoints_check_stores(MEMSPACE mem, unsigned int lastpc, unsigned int pc)
{
    return FALSE;
}
#endif

/* *** CPU INTERFACES *** */


int monitor_force_import(MEMSPACE mem)
{
    return 0;
}

/* called by cpu core */
void monitor_check_icount(uint16_t pc)
{
}

/* called by cpu core */
void monitor_check_icount_interrupt(void)
{
}

/* called by macro DO_INTERRUPT() in 6510(dtv)core.c
 * returns non-zero if breakpoint hit and monitor should be invoked
 */
int monitor_check_breakpoints(MEMSPACE mem, uint16_t addr)
{
    return 0;
}

/* called by macro DO_INTERRUPT() in 6510(dtv)core.c */
void monitor_check_watchpoints(unsigned int lastpc, unsigned int pc)
{
}

int monitor_diskspace_dnr(int mem)
{
    return -1;
}

int monitor_diskspace_mem(int dnr)
{
    return 0;
}

void monitor_change_device(MEMSPACE mem)
{
}
#if 0
static void make_prompt(char *str)
{
}

void monitor_abort(void)
{
}

static void monitor_open(void)
{
}

static int monitor_process(char *cmd)
{
    return 0;
}

static void monitor_close(int check)
{
}
#endif
void monitor_startup(MEMSPACE mem)
{
}
#if 0
static void monitor_trap(uint16_t addr, void *unused_data)
{
}
#endif
void monitor_startup_trap(void)
{
}

void mon_maincpu_toggle_trace(int state)
{
}

void monitor_vsync_hook(void)
{
}


/* 3.5 -> */ 
void mon_update_all_checkpoint_state(void)
{
}

int monitor_is_binary(void)
{
   return 0;
}

ui_jam_action_t monitor_binary_ui_jam_dialog(const char *format, ...)
{
   return UI_JAM_NONE;
}

void monitor_reset_hook(void)
{
}

void monitor_resources_shutdown(void)
{
}
