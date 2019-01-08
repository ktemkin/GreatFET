/*
 * This file is part of libgreat.
 *
 * EHCI-specific USB types.
 */

#include <stdint.h>
#include <stdbool.h>
#include <toolchain.h>

#include <drivers/usb/types.h>
#include <drivers/usb/ehci/platform.h>

#ifndef __USB_EHCI_TYPE_H__
#define __USB_EHCI_TYPE_H__


// From the EHCI specification, section 3.5
typedef struct ehci_transfer_descriptor ehci_transfer_descriptor_t;
struct ehci_transfer_descriptor {

	// DWord 1/2
	volatile ehci_transfer_descriptor_t *next_dtd_pointer;
	volatile ehci_transfer_descriptor_t *alternate_next_dtd_pointer;

	// DWord 3
	struct {
		uint32_t ping_state_err : 1;
		uint32_t split_transaction_state : 1;
		uint32_t missed_uframe : 1;
		uint32_t transaction_error : 1;
		uint32_t babble : 1;
		uint32_t buffer_error : 1;
		uint32_t halted : 1;
		uint32_t active : 1;

		uint32_t pid_code : 2;
		uint32_t error_counter : 2;
		uint32_t current_page : 3;
		uint32_t int_on_complete : 1;
		uint32_t total_bytes : 15;
		uint32_t data_toggle : 1;
	};

	volatile uint32_t buffer_pointer_page[5];
	volatile uint32_t _reserved;
} __attribute__((packed, aligned(64)));

// From Table 3-18 in the EHCI Spec, section 3.6
typedef enum {
	DESCRIPTOR_ITD   = 0,
	DESCRIPTOR_QH    = 1,
	DESCRIPTOR_SITD  = 2,
	DESCRIPTOR_FSTN  = 3
} ehci_data_descriptor_t;

// From the EHCI specificaitons, section 3.1/3.5
typedef union ehci_link ehci_link_t;
union ehci_link {
	// Convenience deviation from the USB spec.
	union ehci_link *ptr;

	uint32_t link;
	struct  {
		uint32_t terminate : 1;
		uint32_t type      : 2;
		uint32_t           : 29;
	};
};


// From the ECHI specification, section 3.6
typedef struct {

	// DWord 1
	ehci_link_t horizontal;

	// DWord 2
	struct {
		uint32_t device_address               : 7;
		uint32_t inactive_on_next_transaction : 1;
		uint32_t endpoint_number              : 4;
		uint32_t endpoint_speed               : 2;
		uint32_t data_toggle_control          : 1;
		uint32_t head_reclamation_flag        : 1;
		uint32_t max_packet_length            : 11;
		uint32_t control_endpoint_flag        : 1;
		uint32_t nak_count_reload             : 4;
	};

	// DWord 3
	struct {
		uint32_t uframe_smask                 : 8;
		uint32_t uframe_cmask                 : 8;
		uint32_t hub_address                  : 7;
		uint32_t port_number                  : 7;
		uint32_t mult                         : 2;
	};

	// Dword 4
	uint32_t current_qtd;

	// Dword 5
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wpacked-not-aligned"
	ehci_transfer_descriptor_t overlay;

	// Any custom data we want, here; the hardware won't
	// touch past the end of the structure above.

} __attribute__((packed, aligned(64))) ehci_queue_head_t;
#pragma GCC diagnostic pop

typedef struct usb_peripheral usb_peripheral_t;

typedef void (*usb_configuration_changed_callback_t)(usb_peripheral_t *callback);


/**
 * Structure describing a dual-mode USB driver that follows the standard
 * EHCI model (host mode) or the common simplified EHCI model (devide mode).
 */
struct usb_peripheral {

	// A reference to the platform-specific collection of registers
	// for the given platform.
	volatile usb_register_block_t *reg;

	/* FIXME: get rid of this! */
	uint8_t controller;

	// Stores whether the USB controller is in host or device mode.
	usb_controller_mode_t mode;

	union {

		// Device mode fields.
		struct {

			// References to each of the relevant device descriptors.
			usb_device_descriptor_t *device_descriptor;
			usb_string_descriptor_list_entry *string_descriptors;
			usb_device_qualifier_descriptor_t *device_qualifier_descriptor;

			// Collections of configuration descriptors for each of the configuration.
			usb_configuration_descriptor_t **full_speed_configurations;
			usb_configuration_descriptor_t **high_speed_configurations;

			// A pointer to the descriptor for the active configuration.
			usb_configuration_descriptor_t *active_configuration;

			// A callback executed each time the configuration is changed.
			usb_configuration_changed_callback_t configuration_changed_callback;

			// Platform-specific additions to the device.
			usb_device_platform_specifics_t device_platform;
		};

		// Host mode fields.
		struct {

			// Head for the asynchronous queue.
			ehci_queue_head_t async_queue_head;

			// TODO: rename me, I'm not really a head?
			ehci_queue_head_t periodic_queue_head;
			ehci_link_t periodic_list[USB_PERIODIC_LIST_SIZE];

			// TODO: support Isochronous trasfers

			// Linked list of pending transfers.
			ehci_link_t pending_transfers;
		};
	};
};


#endif
