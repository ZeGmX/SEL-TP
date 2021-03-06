#include <sys/ptrace.h>
#include <sys/user.h>
#include <stdio.h>
#include <libelf.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#define SIZE 1024

// Finds and returns the PID of the target process
pid_t find_process(char* ownername, char* process) {
    char command[SIZE];

    sprintf(command, "pgrep -u %s %s\n", ownername, process);
    FILE* pgrep = popen(command, "r");

    if (pgrep == NULL) {
        printf("Error : Failed to run pgrep with popen().\n");
        return 0;
    }

    int pid = 0;
    fscanf(pgrep, "%d", &pid);

    if (pclose(pgrep) != 0) {
        printf("Error : pgrep returned a non-zero value.\nAre you sure this process exists ?\n");
        return 0;
    }

    return pid;
}

int get_function_offset(char* target_process, char* target_function) {
    char command[SIZE];
    sprintf(command, "nm %s | grep ' %s'", target_process, target_function);

    FILE* nm = popen(command, "r");
    if (nm == NULL) {
        printf("Error : Failed to run nm with popen().\n");
        return -1;
    }
    unsigned int offset;

    int res = fscanf(nm, "%x %*c %*s", &offset);
    if (res == 0 || res == -1) {
        printf("Error : couldn't match nm output. Are you sure this function exists ?\n");
        return -1;
    }
    pclose(nm);
    return offset;
}

// Returns a pointer to the start of the memory section of the target.
void* get_process_memory(pid_t pid) {
    char filename[SIZE];
    sprintf(filename, "/proc/%d/maps", pid);

    FILE* memory_map = fopen(filename, "r");
    if (memory_map == NULL) {
        printf("Error : Failed to open target process memory map.\n");
        return NULL;
    }

    char buf[SIZE];
    void* address = 0x0;

    if (fscanf(memory_map, "%[0-9a-f][^-]", buf) == 0) {
        return NULL;
    }
    sscanf(buf, "%px", &address);
    fclose(memory_map);

    return address;
}

int write_in_memory(pid_t pid, long address, unsigned char* buffer, int len, unsigned char* override) {
    char mem_path[SIZE];
    sprintf(mem_path, "/proc/%d/mem", pid);

    FILE* process_mem = fopen(mem_path, "r+b");
    if (process_mem == NULL) {
        printf("Error : Failed to open target memory.\n");
        return 0;
    }
    fseek(process_mem, address, SEEK_SET);
    if (override != NULL) {
        fread(override, 1, len, process_mem);
        fseek(process_mem, address, SEEK_SET);
    }

    int res = fwrite(buffer, 1, len, process_mem);
    printf("Wrote %d byte(s) into memory.\n", res);

    fclose(process_mem);
    return res;
}

int run(int argc, char** argv) {
    if (argc != 4) {
        printf("Usage : ./tp [processname] [functionname] [function2]\n");
        return -1;
    }

    char* ownername = getlogin();
    char* target_process = argv[1];
    char* target_function = argv[2];
    char* target_function2 = argv[3];

    pid_t pid = find_process(ownername, target_process);
    if (pid == 0) {
        printf("Could not obtain process ID. Exiting...\n");
        return -1;
    }
    printf("Found process ID : %d\n", pid);

    int status;
    ptrace(PTRACE_ATTACH, pid, NULL, NULL);
    wait(&status);
    // TODO add wait

    // We're now attached to the process, so we should be able to read its memory

    int offset = get_function_offset(target_process, target_function);
    int offset2 = get_function_offset(target_process, target_function2);
    if (offset == -1 || offset2 == -1) {
        printf("Could not obtain target function memory offset. Exiting...\n");
        return -1;
    }
    printf("Target function offset : 0x%x\n", offset);

    void* start_address = get_process_memory(pid);
    if (start_address == NULL) {
        printf("Could not determine target process memory address. Eiting...\n");
        return -1;
    }
    printf("Target process memory starts at : %p\n", start_address);

    //unsigned char override[4];
    unsigned char override[10];

    unsigned char instr_trap = 0xCC;

    if (write_in_memory(pid, (long) start_address+offset, &instr_trap, 1, override) == 0) {
        printf("Could not write in target process memory. Exiting...\n");
        return -1;
    }

    // old_regs is used as a backup, regs is modified and new_regs is used to
    // get the return value
    struct user_regs_struct old_regs, regs, new_regs;

    ptrace(PTRACE_CONT, pid, NULL, NULL);
    wait(&status);
    printf("Got a signal : %s\n", strsignal(WSTOPSIG(status)));

    ptrace(PTRACE_GETREGS, pid, 0, &old_regs);
    ptrace(PTRACE_GETREGS, pid, 0, &regs);

    regs.rax = (long)start_address + offset2;

    // If the function we want to modify has an int or int* argument
    int is_pointer = 1;

    if (is_pointer) {
        // For some reason the immediate value has to be reversed ...
        unsigned char push[5] = {0x68, 0x10, 0x00, 0x00, 0x00};
        unsigned char pop[1] = {0x5F};

        // others arguments would be in rsi and rdx
        regs.rdi = old_regs.rsp - 8; // first argument

        ptrace(PTRACE_SETREGS, pid, 0, &regs);
        unsigned char call[2] = {0xFF, 0xD0};

        write_in_memory(pid, (long)start_address + offset + 1, push, 5, override + 1);
        write_in_memory(pid, (long)start_address + offset + 6, call, 2, override + 6);
        write_in_memory(pid, (long)start_address + offset + 8, pop, 1, override + 8);
        write_in_memory(pid, (long)start_address + offset + 9, &instr_trap, 1, override + 9);
        // TODO all in one write

    } else {
        regs.rdi = 42;
        ptrace(PTRACE_SETREGS, pid, 0, &regs);
        unsigned char call[2] = {0xFF, 0xD0};

        write_in_memory(pid, (long)start_address + offset + 1, call, 2, override + 1);
        write_in_memory(pid, (long)start_address + offset + 3, &instr_trap, 1, override + 3);
    }

    ptrace(PTRACE_CONT, pid, NULL, NULL);
    wait(&status);

    ptrace(PTRACE_GETREGS, pid, 0, &new_regs);
    // Otherwise it is 1 instruction after
    old_regs.rip = (long)start_address + offset;

    if (is_pointer) {
        printf("New pointer value: %lld\n", new_regs.rdi);
        write_in_memory(pid, (long)start_address + offset, override, 10, NULL);
    } else {
        printf("Return value: %lld\n", new_regs.rax);
        write_in_memory(pid, (long)start_address + offset, override, 4, NULL);
    }

    ptrace(PTRACE_SETREGS, pid, 0, &old_regs);

    ptrace(PTRACE_DETACH, pid, NULL, NULL);

    return 0;
}
