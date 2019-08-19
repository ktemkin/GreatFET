/*
 * This file is part of GreatFET
 *
 * Specialized SGPIO interrupt handler for Rhododendron.
 */

 .syntax unified


// Constants that point to registers we'll need to modify in the SGPIO block.
.equ SGPIO_REGISTER_BLOCK_BASE,          0x40101000
.equ SGPIO_SHADOW_REGISTERS_BASE,        0x40101100
.equ SGPIO_EXCHANGE_INTERRUPT_CLEAR_REG, 0x40101F30


// Comment this out if you're sure text is mutable and want the benefits of being in the text section. :)
//.section .data.generated_code
.section .text
.thumb

//
// SGPIO dynamically-generated ISR template
//
.thumb_func
.global rhododendron_isr
rhododendron_isr:

	// Clear the interrupt pending bits for the SGPIO slices we're working with.
	ldr r12, =SGPIO_EXCHANGE_INTERRUPT_CLEAR_REG
	ldr r3, =0xFFFF
	str r3, [r12]

	// Grab the base address of the SGPIO shadow registers...
	ldr r1, =SGPIO_SHADOW_REGISTERS_BASE

	// ... and grab the address of the buffer segment we want to write to.
	ldr r0, =usb_bulk_buffer          // r0 = &buffer
	ldr r12, =usb_position_in_buffer
	ldr r2, [r12]                      // r2 = position_in_buffer
	add r2, r0, r2                    // r0 = write_target = &buffer + position_in_buffer

	push {r12} // Store &position_in_buffer.

	// Our slice chain is set up as follows (ascending data age; arrows are reversed for flow):
	//     L  -> F  -> K  -> C -> J  -> E  -> I  -> A
	// Which has equivalent shadow register offsets:
	//     44 -> 20 -> 40 -> 8 -> 36 -> 16 -> 32 -> 0

	// 8 cycles
	ldr r0,  [r1, #44] // 2
	ldr r3,  [r1, #20] // 1
	ldr r12, [r1, #40] // 1
	str r0,  [r2, #0]  // 2
	str r3,  [r2, #4]  // 1
	str r12, [r2, #8]  // 1

	// 8 cycles
	ldr r0,  [r1, #8 ]
	ldr r3,  [r1, #36]
	ldr r12, [r1, #16]
	str r0,  [r2, #12]
	str r3,  [r2, #16]
	str r12, [r2, #20]

	// 6 cycles
	ldr r0,  [r1, #32] // 2
	ldr r3,  [r1, #0]  // 1
	str r0,  [r2, #24] // 2
	str r3,  [r2, #28] // 1

	// Finally, update the buffer location.
	ldr r0, =0x7fff
	add r1, r2, #32
	and r3, r1, r0         // r0 = (position_in_buffer + size_copied) % buffer_size

	// Restore &position_in_buffer, and store the new position there.
	pop {r1}
	str r3, [r1]           // position_in_buffer = (position_in_buffer + size_copied) % buffer_size

	// and return
	bx lr



