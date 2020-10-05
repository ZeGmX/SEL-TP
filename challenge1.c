#include <sys/ptrace.h>
#include <stdio.h>
#include <sys/user.h>
#include <elf.h>

int main(int argc, char* argv[]) {
  int SIZE = 1024;
  FILE* process;
  char buff[SIZE];
  char* function_name = "update_board_dfs";
  int pid;

  process = popen("pgrep -u lendy 7colors", "r");

  fgets(buff, 64, process);
  printf("PID found: %s", buff);
  pclose(process);

  sscanf(buff, "%d", &pid);  // Get the process pid

  ptrace(PTRACE_ATTACH, pid, NULL, NULL); // Attach to the process

  struct user_regs_struct regs;

  // Get the registers
  // regs.rip is the current instruction pointer
  ptrace(PTRACE_GETREGS, pid, NULL, &regs);

  printf("%llu", regs.rip);

  char path_to_cmdline[SIZE];
  sscanf(buff, "/proc/%s/cmdline", path_to_cmdline);
  printf("%s", path_to_cmdline);

  FILE* file;


  return 0;
}
