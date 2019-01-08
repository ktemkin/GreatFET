/*
 * This file is part of GreatFET
 */

#ifndef __USB_REGISTERS_H__
#define __USB_REGISTERS_H__


typedef union {
	struct {
		uint32_t usb_interrupt        :  1;
		uint32_t usb_error_interrupt  :  1;
		uint32_t port_change_detected :  1;
		uint32_t                      :  1;
		uint32_t system_error         :  1;
		uint32_t                      :  1;
		uint32_t usb_reset_received   :  1;
		uint32_t sof_received         :  1;
		uint32_t dc_suspend           :  1;
		uint32_t                      :  7;
		uint32_t nak_interrupt        :  1;
		uint32_t                      : 15;
	} ATTR_PACKED;
	uint32_t all;
} usb_interrupt_flags_t;


#endif
