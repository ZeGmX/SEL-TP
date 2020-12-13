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
        return 0;
    }
    unsigned int offset;

    int res = fscanf(nm, "%x %*c %*s", &offset);
    if (res == 0 || res == -1) {
        printf("Error : couldn't match nm output. Are you sure this function exists ?\n");
        return 0;
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
        // Couldn't match the output of the memory map
        return NULL;
    }
    // parsing the buffer as an address
    sscanf(buf, "%px", &address);
    fclose(memory_map);

    return address;
}

// Returns a pointer to the start of the libc section of the target
void* get_libc_memory(pid_t pid) {
    char filename[SIZE];
    char command[2*SIZE];
    char buffer[SIZE];

    sprintf(filename, "/proc/%d/maps", pid);
    sprintf(command, "cat %s | grep 'r-xp.*libc'", filename);

    FILE* cat = popen(command, "r");
    if (cat == NULL) {
        printf("Error : Failed to run command with popen().\n");
        return NULL;
    }

    if (fscanf(cat, "%[0-9a-f][^-]", buffer) == 0) {
        return NULL;
    }
    void* address = 0;
    sscanf(buffer, "%px", &address);

    return address;
}

/* Attempts to write into the memory of the process with the given pid.
 * The buffer "buffer" of length "len" will be written at address "address".
 * If an address is given for "override", the overwritten bytes will be stored
 * in that bufffer. 
 * Returns the number of bytes written into memory.
 */
int write_in_memory(pid_t pid, long address, unsigned char* buffer, int len, unsigned char* override) {
    char mem_path[SIZE];
    sprintf(mem_path, "/proc/%d/mem", pid);

    FILE* process_mem = fopen(mem_path, "r+b");
    if (process_mem == NULL) {
        printf("Error : Failed to open target memory.\n");
        return -1;
    }

    fseek(process_mem, address, SEEK_SET);
    if (override != NULL) {
        fread(override, 1, len, process_mem);
        fseek(process_mem, address, SEEK_SET);
    }

    /*int res = */fwrite(buffer, 1, len, process_mem);
    //printf("Wrote %d byte(s) into memory.\n", res);

    fclose(process_mem);
    return 0;
}

/* Reads a binary file containing the code to inject.
 * Returns a pointer to an array containing the code. The pointer given in len
 * then contains the length of the array.
 */
unsigned char* get_injected_code(char* filename, int* len) {
    FILE* binary_file = fopen(filename, "rb");
    
    if (binary_file == NULL) {
        printf("Couldn't open file %s.", filename);
        return NULL;
    }
    fseek(binary_file, 0, SEEK_END);
    int code_size = ftell(binary_file);
    rewind(binary_file);

    unsigned char* code = malloc(sizeof(char)*code_size);
    fread(code, 1, code_size, binary_file);
    fclose(binary_file);

    *len = code_size;
    return code;
}

int run(int argc, char** argv) {
    if (argc != 4) {
        printf("Usage : ./tp [process_name] [function_name] [binary_file]\n");
        exit(128);
    }

    char* ownername = getlogin();
    char* target_process = argv[1];
    char* target_function = argv[2];
    char* binary_filename = argv[3];
    
    int code_size;
    unsigned char* optimized_code = get_injected_code(binary_filename, &code_size);
    if (optimized_code == NULL) {
        printf("Exiting...\n");
        exit(1);
    }

    // Retrieving processes' PID
    pid_t tracer_pid = getpid();
    pid_t target_pid = find_process(ownername, target_process);
    if (target_pid == 0) {
        printf("Could not obtain process ID. Exiting...\n");
        exit(1);
    }
    printf("Found process ID : %d\n", target_pid);

    // Attaching to the target
    int status;
    ptrace(PTRACE_ATTACH, target_pid, NULL, NULL);
    waitpid(target_pid, &status, 0);
    
    /* Computing the addresses of the needed functions.
     * We need to find the address of the target function, libc's memprotect
     * and libc's posix_memalign within the target process' memory.
     */

    // offset of the target function within the target process' binary
    int fun_offset = get_function_offset(target_process, target_function);
    if (fun_offset == 0) {
        printf("Could not find target function offset. Exiting...\n");
        exit(1);
    }

    // addess of the start of the target process' memory section
    void* start_address = get_process_memory(target_pid);
    if (start_address == NULL) {
        printf("Could not obtain target memory section address. Exiting...\n");
        exit(1);
    }
    
    // address of the start of the memory section allocated to libc
    void* tracer_libc = get_libc_memory(tracer_pid);
    if (tracer_libc == NULL) {
        printf("Could not obtain tracer libc address. Exiting...\n");
        exit(1);
    }

    // same with the target
    void* target_libc = get_libc_memory(target_pid);
    if (target_libc == NULL) {
        printf("Could not obtain target libc address. Exiting...\n");
        exit(1);
    }

    /* We observe that the offset of a given libc function within the section
     * allocated to libc is the same for every process (as long as the version
     * of libc is the same). We use this to compute the address of mprotect and
     * posix_memaloign within the target process.
     * This is necessary, as it seems some distros will strip libc's symbols.
     */

    // Computing the offsets of posix_memalign and mprotect 
    long memalign_offset = (long)&posix_memalign - (long)tracer_libc;
    long mprotect_offset = (long)&mprotect - (long)tracer_libc;
    // Computing the addresses
    long memalign_address = (long)target_libc + memalign_offset;
    long mprotect_address = (long)target_libc + mprotect_offset;
    long target_address = (long)start_address + fun_offset;
    
    printf("Found target function address 0x%lx\n", target_address);
    printf("Found posix_memalign address: 0x%lx\n", memalign_address);
    printf("Found mprotect address: 0x%lx\n", mprotect_address);
    printf("----------------\nHijacking target function...\n");

    // Initial code used to call posix_memalign and mprotect
    unsigned char code_call[13] = {
        0xCC,                           // trap
        0x68, 0x00, 0x00, 0x00, 0x00,   // push -> Pointer used by posix_memalign
        0xFF, 0xD0,                     // call rax -> posix_memalign
        0x5F,                           // pop rdi
        0xCC,                           // trap
        0xFF, 0xD0,                     // call rax -> mprotect
        0xCC                            // trap
    };

    // Buffer used to save overwritten bytes in the target process memory
    int size = sizeof(code_call);
    unsigned char overwritten[size];
    
    // Writing the traps and function call into memory
    if (write_in_memory(target_pid, target_address, code_call, size, overwritten) != 0) {
        printf("Could not write in target process memory. Exiting...\n");
        exit(1);
    }

    // Structs used to get target's registers 
    struct user_regs_struct backup_regs, memalign_regs, mprotect_regs;

    // Ordering the target to continue
    ptrace(PTRACE_CONT, target_pid, NULL, NULL);
    waitpid(target_pid, &status, 0);

    // The target should have reached the first trap
    printf("Target reached first trap.\n");
    ptrace(PTRACE_GETREGS, target_pid, 0, &backup_regs);
    ptrace(PTRACE_GETREGS, target_pid, 0, &memalign_regs);

    // Setting up the registers for the call to posix_memalign
    memalign_regs.rax = memalign_address;       // address used by call rax
    memalign_regs.rdi = backup_regs.rsp - 8;    // void** memptr
    memalign_regs.rsi = getpagesize();          // size_t alignement
    memalign_regs.rdx = code_size;              // size_t size

    ptrace(PTRACE_SETREGS, target_pid, 0, &memalign_regs);
    
    // Target can now continue, and should be calling posix_memalign
    ptrace(PTRACE_CONT, target_pid, NULL, NULL);
    waitpid(target_pid, &status, 0);
    
    // Second trap reached. We now repeat a similar process to call mprotect.
    printf("Target reached second trap.\n");
    ptrace(PTRACE_GETREGS, target_pid, 0, &mprotect_regs);
    // We retrieve the output of posix_memalign in rdi
    long allocated_adress = mprotect_regs.rdi;
    printf("Allocated address: 0x%lx\n", allocated_adress);

    mprotect_regs.rax = mprotect_address;
    // rdi is already set by the output of posix_memalign
    mprotect_regs.rsi = code_size;                              //  size_t len
    mprotect_regs.rdx = PROT_EXEC | PROT_READ | PROT_WRITE;     //  int prot
    ptrace(PTRACE_SETREGS, target_pid, 0, &mprotect_regs);
    
    // Target can continue and should be calling mprotect
    ptrace(PTRACE_CONT, target_pid, NULL, NULL);
    waitpid(target_pid, &status, 0);

    /* Third trap reached
     * We can restore target_function's initial state, including registers
     * Then we will write into the allocated space and add the jmp.
     */
    
    ptrace(PTRACE_GETREGS, target_pid, 0, &mprotect_regs);
    if (mprotect_regs.rax != 0) {
        printf("Error while running mprotect in the target process. Exiting...\n");
        exit(1);
    }
    printf("Target process ran mprotect successfully.\n");

    // Restoring the initial code
    printf("Third trap reached. Restoring original code.\n");
    write_in_memory(target_pid, target_address, overwritten, size, NULL);
    
    // Writing the optimised code in the heap
    printf("Writing target code into the heap.\n");
    write_in_memory(target_pid, allocated_adress, optimized_code, code_size, NULL);

    unsigned char trampoline[4] = {
        0x48, 0xB8,                 // mov rax      
        0xFF, 0xE0                  // jmp rax
    };
    // Convert from long to the (reversed) byte array
    union {
        long address;
        unsigned char array[8];
    } add_union;
    add_union.address = allocated_adress;

    // Writing the mov rax, jmp rax
    write_in_memory(target_pid, target_address, trampoline, 2, NULL);
    write_in_memory(target_pid, target_address + 2, add_union.array, 8, NULL);
    write_in_memory(target_pid, target_address + 10, trampoline+2, 2, NULL);

    // Restoring the PC to the start of the target function
    printf("----------------\nRestarting target function.\n");
    backup_regs.rip = target_address;
    ptrace(PTRACE_SETREGS, target_pid, 0, &backup_regs);
    // And we're done
    ptrace(PTRACE_DETACH, target_pid, NULL, NULL);
    
    printf("Done! Exiting gracefully...\n");
    free(optimized_code);
    return 0;
}
