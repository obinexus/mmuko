/* MMUKO x86-64 System V structural ABI binding.
 * arg0: const mmuko_entry_context * in %rdi
 * return: int in %eax
 *
 * The assembly layer does not inspect the C structure. It preserves the
 * structural contract and tail-calls the concern-specific C entry point.
 */
        .text
        .globl mmuko_kernel_entry_asm
        .type mmuko_kernel_entry_asm, @function
mmuko_kernel_entry_asm:
        jmp mmuko_kernel_main@PLT
        .size mmuko_kernel_entry_asm, .-mmuko_kernel_entry_asm

        .globl mmuko_module_entry_asm
        .type mmuko_module_entry_asm, @function
mmuko_module_entry_asm:
        jmp mmuko_module_main@PLT
        .size mmuko_module_entry_asm, .-mmuko_module_entry_asm

        .section .note.GNU-stack,"",@progbits
