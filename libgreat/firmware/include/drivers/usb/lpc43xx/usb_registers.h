/*
 * This file is part of GreatFET
 */

#ifndef __USB_REGISTERS_H__
#define __USB_REGISTERS_H__

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
	volatile uint32_t reserved2[7];

	volatile uint32_t usbcmd;
	volatile uint32_t usbsts;
	volatile uint32_t usbintr;
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
	volatile uint32_t portsc1;
	volatile uint32_t reserved6[7];
	volatile uint32_t otgsc;
	volatile uint32_t usbmode;
	volatile uint32_t endptsetupstat;
	volatile uint32_t endptprime;
	volatile uint32_t endptflush;
	volatile uint32_t endptstat;
	volatile uint32_t endptcomplete;
	volatile uint32_t endptctrl[6];

} usb_register_block_t;

/**
 * Quick references to the USB0 and USB1 register blocks.
 */

#define USB0_REGISTER_BLOCK		((volatile usb_register_block_t *)USB0_BASE)
#define USB1_REGISTER_BLOCK		((volatile usb_register_block_t *)USB1_BASE)

#endif
