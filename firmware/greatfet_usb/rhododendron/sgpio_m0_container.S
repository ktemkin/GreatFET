

//.section .m0.text
.global sgpio_m0_code, sgpio_m0_code_end

sgpio_m0_code:
	.incbin "greatfet_usb_m0.bin"

sgpio_m0_code_end:
	nop

