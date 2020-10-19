#include <sys/ptrace.h>
#include <sys/user.h>
#include <stdio.h>
#include <libelf.h>
#include <sys/wait.h>
#include <unistd.h>
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
    if (res == 0) {
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

int write_in_memory(pid_t pid, long address, unsigned char* buffer, int len) {
    char mem_path[SIZE];
    sprintf(mem_path, "/proc/%d/mem", pid); 
    
    FILE* process_mem = fopen(mem_path, "wb");
    if (process_mem == NULL) {
        printf("Error : Failed to open target memory.\n");
        return 0;
    }
    fseek(process_mem, address, SEEK_SET);

    int res = fwrite(buffer, 1, len, process_mem);
    printf("Wrote %d byte(s) into memory.\n", res);

    fclose(process_mem);
    return res;
}

int run(int argc, char** argv) {
    if (argc != 3) {
        printf("Usage : ./tp [processname] [functionname]\n");
        return -1;
    }

    char* ownername = getlogin();
    char* target_process = argv[1];
    char* target_function = argv[2];

    pid_t pid = find_process(ownername, target_process);
    if (pid == 0) {
        printf("Could not obtain process ID. Exiting...\n");
        return -1;
    }
    printf("Found process ID : %d\n", pid); 
    
    ptrace(PTRACE_ATTACH, pid, NULL, NULL);
    
    // We're now attached to the process, so we should be able to read its memory

    int offset = get_function_offset(target_process, target_function);
    if (offset == -1) {
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
    
    unsigned char instr = 0xCC;

    if (write_in_memory(pid, (long) start_address+offset, &instr, 1) == 0) {
        printf("Could not write in target process memory. Exiting...\n");
        return -1;
    }
    int status;
    struct user_regs_struct regs;
    ptrace(PTRACE_CONT, pid, NULL, NULL);
    while (WSTOPSIG(status) != 5) wait(&status);
    printf("Got a signal : %s\n", strsignal(WSTOPSIG(status)));
    ptrace(PTRACE_GETREGS, pid, 0, &regs);
    printf("%llx\n", regs.rip);
    ptrace(PTRACE_DETACH, pid, NULL, NULL);

    return 0;
}
