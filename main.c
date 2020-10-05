#include <sys/ptrace.h>
#include <sys/user.h>
#include <stdio.h>
#include <elf.h>
#include <libelf.h>
#define SIZE 1024

pid_t find_process(char* ownername, char* process) {
    char command[SIZE];

    // susceptible to buffer overflow attacks if the the user inputs very long strings
    // but whatever
    sprintf(command, "pgrep -u %s %s\n", ownername, process);
    FILE* pgrep = popen(command, "r");

    if (pgrep == NULL) {
        printf("Error : Failed to run pgrep with popen().\n");
        return 0;
    }

    int pid = 0;
    // insecure here as well. sorry
    fscanf(pgrep, "%llu", &pid);
    int ret_code = pclose(pgrep);

    if (ret_code != 0) {
        printf("Error : pgrep returned a non-zero value.\nAre you sure this process exists ?\n");
        return 0;
    } else {
        return pid;
    }
}


int main(int argc, char** argv) {
    if (argc != 3) {
        printf("Usage : ./tp [ownername] [processname]\n");
        return -1;
    }

    pid_t pid = find_process(argv[1], argv[2]);
    if (pid == 0) {
        printf("Could not obtain process ID. Exiting...\n");
        return -1;
    }
    printf("Found process ID : %d\n", pid);

    ptrace(PTRACE_ATTACH, pid, NULL, NULL);

    // We're now attached to the process, so we should be able to read its memory

    FILE* process_mem;
    char mem_path[SIZE];
    char buf[SIZE];

    sprintf(mem_path, "/proc/%d/mem", pid);
    process_mem = fopen(mem_path, "r");

    fseek(process_mem, 0x556b270399a5, SEEK_SET);
    fgets(buf, SIZE, process_mem);
    for(int i = 0; i<SIZE; i++) {
        printf("%x", buf[i]);
    }
    printf("\n");
    /*
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS, pid, NULL, &regs);
    printf("%llu\n", regs.rip);

    Elf *elf;
    file = fopen("7colors", "rb");
    elf = elf_begin(file, ELF_C_READ, NULL);
    */
    return 0;
}
