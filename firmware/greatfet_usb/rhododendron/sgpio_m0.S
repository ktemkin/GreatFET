/*
 * This file is part of GreatFET
 *
 * Specialized SGPIO interrupt handler for Rhododendron.
 */


// Constants that point to registers we'll need to modify in the SGPIO block.
.equ SGPIO_REGISTER_BLOCK_BASE,            0x40101000
.equ SGPIO_SHADOW_REGISTERS_BASE,          0x40101100
.equ SGPIO_EXCHANGE_INTERRUPT_CLEAR_REG,   0x40101F30
.equ SGPIO_EXCHANGE_INTERRUPT_STATUS_REG,  0x40101F2C
.equ USB_BULK_BUFFER,                      0x20008000

// FIXME: pick actual location
.equ POSITION_IN_USB_BUFFER,             0x20007000

.global m0_vector_table

// Create the vector table for the Cortex M0.
.section .text
m0_vector_table:
	.word __StackTop
	.word m0_reset_handler // Reset
	.word fault_handler // NMI
	.word fault_handler // Hard fault.
	.space 28 // Reserved
	.word fault_handler // SVC
	.space 8
	.word fault_handler // PendSV
	.word fault_handler // Systick

	// IRQs
	.rept 32
		.word fault_handler
	.endr




.thumb

.global usb_buffer_position
usb_buffer_position: .word 0


// XXX: intiial debug word to demonstrate that the m0 is working properly
.global validation_word
validation_word: .word 0xdeadbeef


.global m0_reset_handler
.align 4
.thumb_func
m0_reset_handler:

	#ldr r0, =__StackTop
	#msr msp, r0

	ldr r0, =0xC001c0de
	ldr r1, =validation_word
	str r0, [r1]


//
// SGPIO dynamically-generated ISR template
//
.thumb_func
capture_sgpio:

	// Spin until we're ready to handle an SGPIO packet:
	// Grab the exchange interrupt staus...
	ldr r0, =SGPIO_EXCHANGE_INTERRUPT_STATUS_REG
	ldr r0, [r0]

	// ... check to see if it has any interrupt bits set...
	lsr r0, #1

	// ... and if not, jump back to the beginning.
	bcc capture_sgpio

	// Clear the interrupt pending bits for the SGPIO slices we're working with.
	ldr r0, =SGPIO_EXCHANGE_INTERRUPT_CLEAR_REG
	ldr r1, =0xffff
	str r1, [r0]

	// Grab the base address of the SGPIO shadow registers...
	ldr r7, =SGPIO_SHADOW_REGISTERS_BASE

	// ... and grab the address of the buffer segment we want to write to.
	ldr r0, =USB_BULK_BUFFER          // r0 = &buffer
	ldr r3, =usb_buffer_position      // r3 = &position_in_buffer
	ldr r2, [r3]                      // r2 = position_in_buffer
	add r6, r0, r2                    // r0 = write_target = &buffer + position_in_buffer

	mov r8, r3                        // Store &position_in_buffer.

	// Our slice chain is set up as follows (ascending data age; arrows are reversed for flow):
	//     L  -> F  -> K  -> C -> J  -> E  -> I  -> A
	// Which has equivalent shadow register offsets:
	//     44 -> 20 -> 40 -> 8 -> 36 -> 16 -> 32 -> 0

	// 8 cycles
	ldr r0,  [r7, #44] // 2
	ldr r1,  [r7, #20] // 2
	ldr r2,  [r7, #40] // 2
	ldr r3,  [r7, #8 ] // 2
	ldr r4,  [r7, #36] // 2
	ldr r5,  [r7, #16] // 2
	stm r6!, {r0-r5}   // 7

	// 6 cycles
	ldr r0,  [r7, #32] // 2
	ldr r1,  [r7, #0]  // 2
	stm r6!, {r0-r1}

	// Finally, update the buffer location.
	ldr r0, =0x7fff        // r0 => modulus flags
	and r0, r6, r0         // r0 = (position_in_buffer + size_copied) % buffer_size

	// Restore &position_in_buffer, and store the new position there.
	mov r1, r8
	str r0, [r1]           // position_in_buffer = (position_in_buffer + size_copied) % buffer_size

	b capture_sgpio


#
# Simple "trap here" fault handler.
#
.thumb_func
fault_handler:
	b fault_handler
