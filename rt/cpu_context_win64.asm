.code


rt_task_trampoline PROC FRAME
  .endprolog
  pop  rcx
  pop  rax
  call rax
  ud2
rt_task_trampoline ENDP


; void rt_cpu_context_switch(CpuContext* to)
rt_cpu_context_switch PROC FRAME
  .endprolog

  ; load xmm registers
  movaps xmm6,  [rcx]
  movaps xmm7,  [rcx+010h]
  movaps xmm8,  [rcx+020h]
  movaps xmm9,  [rcx+030h]
  movaps xmm10, [rcx+040h]
  movaps xmm11, [rcx+050h]
  movaps xmm12, [rcx+060h]
  movaps xmm13, [rcx+070h]
  movaps xmm14, [rcx+080h]
  movaps xmm15, [rcx+090h]

  ; load mmx control and status word
  ldmxcsr [rcx+0a0h]

  ; load x87 control word
  fldcw [rcx+0a4h]

  ; load non-volatile registers
  mov r12, [rcx+0a8h]
  mov r13, [rcx+0b0h]
  mov r14, [rcx+0b8h]
  mov r15, [rcx+0c0h]
  mov rdi, [rcx+0c8h]
  mov rsi, [rcx+0d0h]
  mov rbx, [rcx+0d8h]
  mov rbp, [rcx+0e0h]
  mov rsp, [rcx+0e8h]
  push [rcx+0f0h]
  ret
rt_cpu_context_switch ENDP


; void rt_cpu_context_swap(CpuContext* from (RCX), const CpuContext* to (RDX))
rt_cpu_context_swap PROC FRAME
  .endprolog
  ; save xmm registers
  movaps [rcx],      xmm6
  movaps [rcx+010h], xmm7
  movaps [rcx+020h], xmm8
  movaps [rcx+030h], xmm9
  movaps [rcx+040h], xmm10
  movaps [rcx+050h], xmm11
  movaps [rcx+060h], xmm12
  movaps [rcx+070h], xmm13
  movaps [rcx+080h], xmm14
  movaps [rcx+090h], xmm15

  ; save mmx control and status word
  stmxcsr [rcx+0a0h]

  ; save x87 control word
  fnstcw [rcx+0a4h]

  ; save non-volatile registers
  mov [rcx+0a8h], r12
  mov [rcx+0b0h], r13
  mov [rcx+0b8h], r14
  mov [rcx+0c0h], r15
  mov [rcx+0c8h], rdi
  mov [rcx+0d0h], rsi
  mov [rcx+0d8h], rbx
  mov [rcx+0e0h], rbp
  mov [rcx+0e8h], rsp
  lea rax, restore_point
  mov [rcx+0f0h], rax

  ; load xmm registers
  movaps xmm6,  [rdx]
  movaps xmm7,  [rdx+010h]
  movaps xmm8,  [rdx+020h]
  movaps xmm9,  [rdx+030h]
  movaps xmm10, [rdx+040h]
  movaps xmm11, [rdx+050h]
  movaps xmm12, [rdx+060h]
  movaps xmm13, [rdx+070h]
  movaps xmm14, [rdx+080h]
  movaps xmm15, [rdx+090h]

  ; load mmx control and status word
  ldmxcsr [rdx+0a0h]

  ; load x87 control word
  fldcw [rdx+0a4h]

  ; load non-volatile registers
  mov r12, [rdx+0a8h]
  mov r13, [rdx+0b0h]
  mov r14, [rdx+0b8h]
  mov r15, [rdx+0c0h]
  mov rdi, [rdx+0c8h]
  mov rsi, [rdx+0d0h]
  mov rbx, [rdx+0d8h]
  mov rbp, [rdx+0e0h]
  mov rsp, [rdx+0e8h]
  push [rdx+0f0h]

restore_point:
  ret

rt_cpu_context_swap ENDP

END