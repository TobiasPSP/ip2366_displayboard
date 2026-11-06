  .syntax unified
  .cpu cortex-m0plus
  .thumb

  .global Reset_Handler
  .global g_pfnVectors

  .section .text.Reset_Handler,"ax",%progbits
  .type Reset_Handler, %function
Reset_Handler:
  bl main
  b .
  .size Reset_Handler, .-Reset_Handler

  .section .isr_vector,"a",%progbits
  .type g_pfnVectors, %object
g_pfnVectors:
  .word _estack
  .word Reset_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word Default_Handler
  .word 0
  .word 0
  .word 0
  .word 0
  .word Default_Handler
  .word Default_Handler
  .word 0
  .word Default_Handler
  .word Default_Handler
  .size g_pfnVectors, .-g_pfnVectors

  .section .text.Default_Handler,"ax",%progbits
  .type Default_Handler, %function
Default_Handler:
  b .
  .size Default_Handler, .-Default_Handler
