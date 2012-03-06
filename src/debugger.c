#include "hal.h"
#include "readline.h"
#include "string.h"
#include "stdio.h"

#define DEBUG_IPI 0

#define MAX_CMDS 32

static volatile int in_debugger = 0;
static volatile int num_cores_in_debugger = 0;

static volatile core_debug_state_t states[MAX_CORES];

typedef struct cmd {
  const char *cmd;
  const char *help;
  debugger_fn_t fn;
} cmd_t;

static int num_cmds = 0;
static cmd_t cmds[MAX_CMDS];

static void print_tabular(const char *str, int n) {
#define NUM_COLS 4

  if (n != 0 && n % NUM_COLS == 0 )
    kprintf("\n");
  kprintf("%-20s", str);
}

static int get_unambiguous_cmd(const char *cmd) {
  /* Scan for the first space. */
  int len = 0;
  while (cmd[len] != '\0' && cmd[len] != ' ')
    ++len;
  for (; len != 0; --len) {
    int matches = 0;
    int match = -1;
    for (int i = 0; i < num_cmds; ++i) {
      if (!strncmp(cmd, cmds[i].cmd, len)) {
        ++matches;
        match = i;
      }
    }

    if (matches == 1)
      return match;
    else if (matches > 1)
      return -1;
  }
  return -1;
}

static void print_ambiguous(const char *cmd) {
  for (int len = strlen(cmd); len != 0; --len) {
    int matches = 0;
    for (int i = 0; i < num_cmds; ++i) {
      if (!strncmp(cmd, cmds[i].cmd, len))
        ++matches;
    }

    if (matches == 1) {
      /* Shouldn't get here. */
      kprintf("Algorithmic error in debugger!");
      return;
    } else if (matches > 1) {
      kprintf("%s is ambiguous - did you mean one of these?:\n", cmd);

      matches = 0;
      for (int i = 0; i < num_cmds; ++i) {
        if (!strncmp(cmd, cmds[i].cmd, len))
          print_tabular(cmds[i].cmd, matches++);
      }
      kprintf("\n");
      return;
    }
  }

  kprintf("%s is not a known command.\n", cmd);
}

static void help(const char *cmd, core_debug_state_t *states) {
  /* Strip the "help" and following whitespace off the front. */
  cmd = &cmd[4];
  while (*cmd == ' ')
    ++cmd;
  
  /* No parameters, list all commands. */
  if (*cmd == '\0') {
    for (int i = 0; i < num_cmds; ++i)
      kprintf("%10s - %s\n", cmds[i].cmd, cmds[i].help);
  } else {
    int c = get_unambiguous_cmd(cmd);
    if (c != -1)
      kprintf("%s\n", cmds[c].help);
    else
      print_ambiguous(cmd);
  }
}

static void stop_other_processors() {
  send_ipi(-2, (void*)DEBUG_IPI);
}

static void save_backtrace() {
  uintptr_t data = 0;
  uintptr_t bt;

  int id = get_processor_id();
  if (id == -1) id = 0;

  for (int i = 0; i < MAX_BACKTRACE; ++i) {
    bt = backtrace(&data);
    states[id].backtrace[i] = bt;
    if (bt == 0) break;
  }
}

static void save_regs(struct regs *regs) {
  int id = get_processor_id();
  if (id == -1) id = 0;

  states[id].registers = regs;
}

static void do_repl() {
  char line[256];
  while (1) {
    readline(line, 256, "(db) ", NULL);

    if (!strcmp(line, "exit"))
      break;

    int id = get_unambiguous_cmd(line);
    if (id == -1)
      print_ambiguous(line);
    else
      cmds[id].fn(line, (core_debug_state_t*)states);
      
  }
}

static void do_debug() {
  num_cores_in_debugger = 0;
  in_debugger = 1;
  stop_other_processors();

  int num_other_processors = get_num_processors();
  if (num_other_processors != -1)
    while (num_cores_in_debugger != num_other_processors)
      ;
  save_backtrace();
  
  kprintf("*** Kernel debugger entered from core #%d\n",
          get_processor_id() == -1 ? 0 : get_processor_id());
  
  do_repl();

  /* Allow other cores to continue. */
  in_debugger = 0;
}

void debugger_trap(struct regs *regs) {
  save_regs(regs);
  do_debug();
}

void debugger_except(struct regs *regs, const char *description) {
  save_regs(regs);
  kprintf("*** Exception: %s\n", description);
  do_debug();
}

static int debugger_handle_ipi(struct regs *regs, void *p) {
  void *value = get_ipi_data(regs);
  if (value == (void*)DEBUG_IPI) {
    int ints = get_interrupt_state();
    enable_interrupts();

    __sync_fetch_and_add(&num_cores_in_debugger, 1);
    save_backtrace();

    while (in_debugger)
      ;

    set_interrupt_state(ints);
  }
  return 0;
}

int register_debugger_handler(const char *name, const char *help,
                              debugger_fn_t fn) {
  if (num_cmds >= MAX_CMDS)
    return -1;

  cmds[num_cmds].cmd = name;
  cmds[num_cmds].help = help;
  cmds[num_cmds].fn   = fn;
  ++num_cmds;

  return 0;
}

static int debugger_register() {

  if (get_ipi_interrupt_num() != -1 &&
      register_interrupt_handler(get_ipi_interrupt_num(),
                                 &debugger_handle_ipi,
                                 NULL) == -1) {
    kprintf("Unable to register interrupt handler for IPIs!\n");
    return -1;
  }

  if (register_debugger_handler("help", "Display help for a command.",
                                &help) == -1) {
    kprintf("Unable to register 'help' debugger handler!\n");
    return -1;
  }

  return 0;
}

static const char *p[] = {"interrupts", NULL};
static init_fini_fn_t x run_on_startup = {
  .name = "debugger",
  .prerequisites = p,
  .fn = &debugger_register
};
