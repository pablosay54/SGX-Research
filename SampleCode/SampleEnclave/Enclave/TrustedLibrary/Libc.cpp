/*
 * Copyright (C) 2011-2019 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


#include <string.h>
#include <time.h>

#include "sgx_cpuid.h"
#include "../../../../linux-sgx/common/inc/internal/rts.h"
#include "../../../../linux-sgx/sdk/trts/trts_emodpr.h"
#include "../../../../linux-sgx/common/inc/internal/arch.h"

#include "sgx_trts.h"
#include "../Enclave.h"
#include "Enclave_t.h"
#include "sgx_arch.h"
#define BILLION  1000000000.0

/* ecall_malloc_free:
 *   Uses malloc/free to allocate/free trusted memory.
 */
void ecall_malloc_free(void)
{
    void *ptr = malloc(100);
    assert(ptr != NULL);
    memset(ptr, 0x0, 100);
    free(ptr);
}

/* ecall_sgx_cpuid:
 *   Uses sgx_cpuid to get CPU features and types.
 */
void ecall_sgx_cpuid(int cpuinfo[4], int leaf)
{
    sgx_status_t ret = sgx_cpuid(cpuinfo, leaf);
    if (ret != SGX_SUCCESS)
        abort();
}

extern "C" sgx_status_t trts_mprotect(size_t start, size_t size, uint64_t perms);
extern "C" sgx_status_t trts_mmap(size_t start, size_t size);
extern "C" sgx_status_t trts_munmap(size_t start, size_t size);
extern "C" sgx_status_t NesTEE_trts_mprotect(size_t start,size_t size, uint64_t perms);

extern uint8_t __ImageBase;

#if 1
void __attribute__((section(".nestee_entry"), unused))
NesTEE_Gateway(size_t page, size_t *stack, size_t *fun_addr, size_t *secinfo_RWX, size_t *secinfo_R, size_t size)
{
   __asm__ __volatile__(
        // Unprotect NesTEE Page
        "movq $0x6, %%rax \n" //setting
        "movq %3, %%rbx \n" //sec info
        "movq %%rdi, %%rcx \n" //address of the destination EPC page
        "ENCLU \n"
        
        //Check ENCLU parameters
        "cmp $0x6, %%rax\n" 
        // "jne crash_entry \n"
        "cmp %%rdi, %%rcx \n" 
        // "jne crash_entry \n"
        
        // Set up secure stack
        "movq %%rsp, %%rbx \n"
        "movq %1, %%rsp \n"
        "push %%rbx \n"

        // must save params prior to call as they are not saved across calls
        "push %%rdi\n"
        "push %%r9 \n"
        	
        // Go to NesTEE LibOS
        "call %2 \n" 

        // Pop registers from stack
        "pop %%r9 \n"
        "pop %%rdi \n"

        // protect the NesTEE page using saved registers
        "movq $0x5, %%rax \n" //setting
        "movq %%r9, %%rbx \n" //sec info
        "movq %%rdi, %%rcx \n" //address of destination EPC page
        "ENCLU \n"

        // check enclu parameters
        "cmp $0x5, %%rax \n"
        // "jne crash_exit \n"
        "cmp %%r9, %%rbx \n"
        // "jne crash_exit \n"
        "cmp %%rdi, %%rcx \n"
        // "jne crash_exit \n"

        // restore user stack
        "pop %%rbx \n"
        "movq %%rbx, %%rsp \n"

        :: "D" ((uint64_t) page), 
        "S" ((uint64_t) stack), 
        "r" ((uint64_t) fun_addr),
        "r" ((uint64_t) secinfo_RWX),
	"r" ((uint64_t) secinfo_R),
	"r"((uint64_t) size):
        );  
}
#endif

void
__attribute__((section(".security_monitor"), unused))
helloWorld (void)
{
    //printf("Hello World");
    
    /* Lock up NesTEE pages */
    uint64_t perms = 0x1;
    
    //ocall and protect NesTEE pages
    int rc = -1;
    sgx_status_t ret = SGX_SUCCESS;
    SE_DECLSPEC_ALIGN(sizeof(sec_info_t)) sec_info_t si;
    
    void* hello_world_ptr = (void *)&helloWorld;
    size_t size = 4096;
    size_t start = ((uintptr_t)hello_world_ptr + 4096-1) & ~(4096-1);

    NesTEE_trts_mprotect(start, size, EDMM_NESTEE_MODPR);
    
}

void ecall_test_mprotect(void)
{
    void* hello_world_ptr = (void *) &helloWorld; //test entry function
    size_t size = 4096;

    //align start to page boundary 
    size_t start = ((uintptr_t)hello_world_ptr +  4096 - 1) & ~(4096  - 1);

    /* protect a single page, then add it to the ecall_test_mprotect */
    // allocate the extra page
    size_t stack = (size_t) malloc(4096);
    //align to page boundary & protect
    stack = (stack +  4096 - 1) & ~(4096  - 1);
    trts_mprotect(stack, 4096, 0x7);    
    trts_mprotect(start, size, 0x7);

    // printf("Addr: %zx\n", start);
    sgx_arch_sec_info_t secinfo_RWX;
    sgx_arch_sec_info_t secinfo_R;
    memset(&secinfo_RWX, 0, sizeof(sgx_arch_sec_info_t));
    memset(&secinfo_R, 0, sizeof(sgx_arch_sec_info_t));
    secinfo_RWX.flags = 0x7;
    secinfo_R.flags = 0x1;
    
    NesTEE_Gateway(start, (size_t *) stack, (size_t *) hello_world_ptr, (size_t *) &secinfo_RWX, (size_t *) &secinfo_R, size);
}
