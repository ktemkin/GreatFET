/*
 * This file is part of libgreat.
 *
 * Platform-specific
 */

#ifndef __USB_PLATFORM_H__
#define __USB_PLATFORM_H__

#include <drivers/usb/ehci/registers.h>
#include <drivers/usb/ehci/device_queue.h>

#include <drivers/usb/types.h>


typedef struct {

	// Collection of USB device Queue Heads (dQH).
	usb_queue_head_t queue_heads_device[USB_TOTAL_QUEUE_HEADS] ATTR_ALIGNED(2048);

} usb_device_platform_specifics_t;


/**
 * Structure describing the USB register blocks.
 * Used so we don't have to have tons of constants floating around.
 */
typedef struct {
	volatile uint32_t reserved0[64];
	volatile uint32_t caplength;
	volatile uint32_t hcsparams;
	volatile uint32_t hccparams;
	volatile uint32_t reserved1[5];
	volatile uint32_t dciversion;
	volatile uint32_t dccparams;
	volatile uint32_t reserved2[6]; /* 0x128, 0x12c, 0x130, 0x134, 0x138, 0x13c */

	volatile uint32_t usbcmd;

	volatile usb_interrupt_flags_t usbsts;
	volatile usb_interrupt_flags_t usbintr;

	volatile uint32_t frindex;

	volatile uint32_t reserved3;

	union {
		 volatile uint32_t periodiclistbase;
		 volatile uint32_t deviceaddr;
	};

	union {
		 volatile uint32_t asynclistaddr;
		 volatile uint32_t endpointlistaddr;
	};

	volatile uint32_t ttctrl;
	volatile uint32_t burstsize;
	volatile uint32_t txfilltuning;
	volatile uint32_t reserved4[2];
	volatile uint32_t ulpiviewport;
	volatile uint32_t binterval;
	volatile uint32_t endptnak;
	volatile uint32_t endptnaken;
	volatile uint32_t reserved5;

	/**
	 * Port status and control register.
	 */
	union {
		volatile uint32_t all;
		struct {
			uint32_t connection                  :  1;
			uint32_t connection_status_changed   :  1;
			uint32_t port_enable                 :  1;
			uint32_t port_enable_changed         :  1;
			uint32_t over_current_active         :  1;
			uint32_t over_current_changed        :  1;
			uint32_t force_port_resume           :  1;
			uint32_t in_suspend                  :  1;
			uint32_t port_reset                  :  1;
			uint32_t connection_is_high_speed    :  1;
			uint32_t line_status                 :  2;
			uint32_t port_power_state            :  1;
			uint32_t                             :  1;
			uint32_t port_indicator_state        :  2;
			uint32_t port_test_control           :  4;
			uint32_t wake_on_connect_enabled     :  1;
			uint32_t wake_on_disconnect_enabled  :  1;
			uint32_t wake_on_overcurrent_enabled :  1;
			uint32_t disable_phy_clock           :  1;
			uint32_t force_full_speed            :  1;
			uint32_t                             :  1;
			uint32_t current_port_speed          :  2;
			uint32_t                             :  4;

		} ATTR_PACKED;
	} portsc1;

	volatile uint32_t reserved6[7];
	volatile uint32_t otgsc;
	volatile uint32_t usbmode;
	volatile uint32_t endptsetupstat;
	volatile uint32_t endptprime;
	volatile uint32_t endptflush;
	volatile uint32_t endptstat;
	volatile uint32_t endptcomplete;
	volatile uint32_t endptctrl[6];

} ATTR_PACKED usb_register_block_t;

/**
 * Quick references to the USB0 and USB1 register blocks.
 */

#define USB0_REGISTER_BLOCK		((volatile usb_register_block_t *)USB0_BASE)
#define USB1_REGISTER_BLOCK		((volatile usb_register_block_t *)USB1_BASE)

#endif
