#include <sys/ptrace.h>
#include <sys/user.h>
#include <stdio.h>
#include <libelf.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
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

// Returns a pointer to the start of the code of the target
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

// Returns a pointer to the start of the libc section of the target
void* get_libc_memory(pid_t pid) {
    char filename[SIZE];
    char command[2 * SIZE];
    char buffer[SIZE];

    sprintf(filename, "/proc/%d/maps", pid);
    sprintf(command, "cat %s | grep 'r-xp.*libc'", filename);

    FILE* cat = popen(command, "r");
    if (cat == NULL) {
        printf("Error : Failed to run cat with popen().\n");
        return NULL;
    }

    if (fscanf(cat, "%[0-9a-f][^-]", buffer) == 0) {
        return NULL;
    }
    void* address = 0;
    sscanf(buffer, "%px", &address);

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
        printf("Usage : ./tp [processname] [functionname] [sizetowrite]\n");
        return -1;
    }

    char* ownername = getlogin();
    char* target_process = argv[1];
    char* target_function = argv[2];
    int sizetowrite = atoi(argv[3]);

    pid_t tracer_pid = getpid();
    pid_t tracee_pid = find_process(ownername, target_process);
    if (tracee_pid == 0) {
        printf("Could not obtain process ID. Exiting...\n");
        return -1;
    }
    printf("Found process ID : %d\n", tracee_pid);

    int status;
    ptrace(PTRACE_ATTACH, tracee_pid, NULL, NULL);
    wait(&status);

    int fun_offset = get_function_offset(target_process, target_function);
    void* start_address = get_process_memory(tracee_pid);
    void* tracer_libc = get_libc_memory(tracer_pid);
    void* tracee_libc = get_libc_memory(tracee_pid);

    // Computing the addres of the posix_memalign and mprotect functions
    long posix_offset = (long)&posix_memalign - (long)tracer_libc;
    long mprotect_offset = (long)&mprotect - (long)tracer_libc;
    long posix_memalign_address = (long)tracee_libc + posix_offset;
    long mprotect_address = (long)tracee_libc + mprotect_offset;

    printf("Found posix_memalign address: %lx\n", posix_memalign_address);
    printf("Found mprotect address: %lx\n", mprotect_address);

    // Initial code for calling posix_memalign and mprotect
    unsigned char code_call[] = {
        0xCC,                           // trap
        0x68, 0x00, 0x00, 0x00, 0x00,   // push -> Pointer that will be overriden by posix_memalign
        0xFF, 0xD0,                     // call rax -> posix_memalign
        0x5F,                           // pop rdi
        0xCC,                           // trap
        0xFF, 0xD0,                     // call rax -> mprotect
        0xCC                            //trap
    };
    // To save the overriden instructions
    unsigned char override[sizeof(code_call)];

    if (write_in_memory(tracee_pid, (long) start_address+fun_offset, code_call, sizeof(code_call), override) == 0) {
        printf("Could not write in target process memory. Exiting...\n");
        return -1;
    }

    // old_regs is used as a backup, regs is modified and new_regs is used to
    // get the return value
    struct user_regs_struct old_regs, regs, new_regs;

    ptrace(PTRACE_CONT, tracee_pid, NULL, NULL);

    wait(&status);
    // The more the merier
    // Don't ask any question
    wait(&status);
    wait(&status);
    // *** First trap ***

    printf("Got a signal : %s\n", strsignal(WSTOPSIG(status)));

    ptrace(PTRACE_GETREGS, tracee_pid, 0, &old_regs);
    ptrace(PTRACE_GETREGS, tracee_pid, 0, &regs);

    // Adress called
    regs.rax = posix_memalign_address;
    // Arguments of posix_memalign
    regs.rdi = old_regs.rsp - 8;    // void** memptr
    regs.rsi = getpagesize();       // size_t alignement
    regs.rdx = sizetowrite;         // size_t size

    ptrace(PTRACE_SETREGS, tracee_pid, 0, &regs);

    ptrace(PTRACE_CONT, tracee_pid, NULL, NULL);
    wait(&status);
    // *** Second trap ***

    ptrace(PTRACE_GETREGS, tracee_pid, 0, &new_regs);

    long allocated_adress = new_regs.rdi;
    printf("Allocated address: %lx\n", allocated_adress);

    new_regs.rax = mprotect_address;
    // rdi is already ok -          void* address
    new_regs.rsi = sizetowrite; //  size_t len
    new_regs.rdx = PROT_EXEC | PROT_READ | PROT_WRITE;   //  int prot
    ptrace(PTRACE_SETREGS, tracee_pid, 0, &new_regs);

    ptrace(PTRACE_CONT, tracee_pid, NULL, NULL);
    wait(&status);
    // ** Third trap **
    // At the end -> restauration and writing the jmp and the function code

    // Restoring the initial code
    write_in_memory(tracee_pid, (long) start_address + fun_offset, override, sizeof(override), NULL);

    ptrace(PTRACE_GETREGS, tracee_pid, 0, &new_regs);
    printf("memprotec's return value: %lld\n", new_regs.rax);

    old_regs.rip = (long)start_address + fun_offset;

    // Code of the "optimised" function
    unsigned char function_code[11] = {
        0x55, 0x48, 0x89, 0xe5, 0xb8, 0x01, 0x00, 0x00, 0x00, 0x5d, 0xc3
    };

    if (sizetowrite != 11) {
        printf("This is not meant to wrint code with size different to 11, got %d\n", sizetowrite);
        return -1;
    }

    // Writing the optimised code on the heap
    write_in_memory(tracee_pid, allocated_adress, function_code, sizetowrite, NULL);

    unsigned char move_rax[2] = {0x48, 0xB8};
    unsigned char jump_rax[2] = {0xFF, 0xE0};
    // To automatically convert from long to the (reversed) byte array
    union {
        long address;
        unsigned char array[8];
    } add_union;
    add_union.address = allocated_adress;

    // Writing the mov rax, jmp rax
    write_in_memory(tracee_pid, (long)start_address + fun_offset, move_rax, 2, NULL);
    write_in_memory(tracee_pid, (long)start_address + fun_offset + 2, add_union.array, 8, NULL);
    write_in_memory(tracee_pid, (long)start_address + fun_offset + 10, jump_rax, 2, NULL);

    ptrace(PTRACE_SETREGS, tracee_pid, 0, &old_regs);
    ptrace(PTRACE_DETACH, tracee_pid, NULL, NULL);

    return 0;
}
