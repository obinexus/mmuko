/* MMUKO AArch64 AAPCS64 structural ABI binding.
 * arg0: const mmuko_entry_context * in x0
 * return: int in w0
 */
        .text
        .globl mmuko_kernel_entry_asm
        .type mmuko_kernel_entry_asm, %function
mmuko_kernel_entry_asm:
        b mmuko_kernel_main
        .size mmuko_kernel_entry_asm, .-mmuko_kernel_entry_asm

        .globl mmuko_module_entry_asm
        .type mmuko_module_entry_asm, %function
mmuko_module_entry_asm:
        b mmuko_module_main
        .size mmuko_module_entry_asm, .-mmuko_module_entry_asm

        .section .note.GNU-stack,"",%progbits
