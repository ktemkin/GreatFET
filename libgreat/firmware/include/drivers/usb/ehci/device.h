/*
 * This file is part of GreatFET
 */

#ifndef __USB_DEVICE_H__
#define __USB_DEVICE_H__

// TODO: Refactor to support high performance operations without having to
// expose usb_transfer_descriptor_t. Or usb_endpoint_prime(). Or, or, or...
#include <libopencm3/cm3/vector.h>

#include <drivers/usb/types.h>
#include <drivers/usb/ehci/types.h>

#define NUM_USB_CONTROLLERS 2
#define NUM_USB1_ENDPOINTS 4


extern usb_peripheral_t usb_peripherals[2];

void usb_peripheral_reset();

/**
 * Handles a host-issued USB bus reset -- effectively setting up the device controller
 * for a new burst of communications.
 */
void usb_handle_bus_reset(usb_peripheral_t *const device);

usb_queue_head_t* usb_queue_head(
	const uint_fast8_t endpoint_address,
	usb_peripheral_t* const device
);


usb_endpoint_t* usb_endpoint_from_address(
	const uint_fast8_t endpoint_address,
	usb_peripheral_t* const device
);

uint_fast8_t usb_endpoint_address(
	const usb_transfer_direction_t direction,
	const uint_fast8_t number
);


void usb_phy_enable(
	const usb_peripheral_t* const device
);


void usb_set_irq_handler(
	usb_peripheral_t* const device,
	vector_table_entry_t isr
);

void usb_device_init(
	usb_peripheral_t* const device
);

void usb_controller_reset(
	usb_peripheral_t* const device
);


void usb_controller_run(
	const usb_peripheral_t* const device
);


/**
 * Disables the ability for the given port to connect at high speed.
 *
 * Useful for debugging high-speed-specific modes or viewing things
 * with more primitive USB analyzers.
 */
void usb_prevent_high_speed(usb_peripheral_t *device);

/**
 * Cancels the effects of a previous usb_prevent_high_speed(), re-enabling
 * the abiltity for a device to connect at high speeds.
 */
void usb_allow_high_speed(usb_peripheral_t *device);


void usb_run(
	usb_peripheral_t* const device
);

void usb_run_tasks(
	const usb_peripheral_t* const device
);

usb_speed_t usb_speed(
	const usb_peripheral_t* const device
);


usb_interrupt_flags_t usb_get_status(const usb_peripheral_t* const device);

uint32_t usb_get_endpoint_setup_status(
	const usb_peripheral_t* const device
);

void usb_clear_endpoint_setup_status(
	const uint32_t endpoint_setup_status,
	const usb_peripheral_t* const device
);


uint32_t usb_get_endpoint_ready(
	const usb_peripheral_t* const device
);

uint32_t usb_get_endpoint_complete(
	const usb_peripheral_t* const device
);

void usb_clear_endpoint_complete(
	const uint32_t endpoint_complete,
	const usb_peripheral_t* const device
);

void usb_set_address_immediate(
	const usb_peripheral_t* const device,
	const uint_fast8_t address
);

void usb_set_address_deferred(
	const usb_peripheral_t* const device,
	const uint_fast8_t address
);

void usb_configure_endpoint_queue_head(usb_endpoint_t *endpoint,
		uint16_t max_packet_size, usb_transfer_type_t transfer_type);

void usb_in_endpoint_enable_nak_interrupt(
	const usb_endpoint_t* const endpoint
);

void usb_in_endpoint_disable_nak_interrupt(
	const usb_endpoint_t* const endpoint
);

void usb_endpoint_init(usb_endpoint_t *endpoint);

void usb_endpoint_stall(
	const usb_endpoint_t* const endpoint
);

void usb_endpoint_disable(
	const usb_endpoint_t* const endpoint
);

void usb_endpoint_flush(
	const usb_endpoint_t* const endpoint
);

bool usb_endpoint_is_ready(
	const usb_endpoint_t* const endpoint
);

void usb_endpoint_prime(
	const usb_endpoint_t* const endpoint,
	usb_transfer_descriptor_t* const first_td
);

void usb_endpoint_schedule_wait(
	const usb_endpoint_t* const endpoint,
        usb_transfer_descriptor_t* const td
);

void usb_endpoint_schedule_append(
        const usb_endpoint_t* const endpoint,
        usb_transfer_descriptor_t* const tail_td,
        usb_transfer_descriptor_t* const new_td
);


void usb_copy_setup(usb_setup_t* const dst, const volatile uint8_t* const src);

/**
 * Apply a given configuration to the USB device.
 *
 * @param configuration_value The configuration value for the given configuration,
 *		as denoted in the relevant configuration descriptor, or 0 to de-configure the device.
 *
 * @return 0 on success, or an error code on failure.
 */
int usb_set_configuration(usb_peripheral_t *device, uint8_t configuration_number);


/**
 * Finds the configuration descriptor associated with the given value.
 *
 * @param configuration_value The value to search for a descriptor for.
 * @return A pointer to the configuration descriptor, or NULL if none could be found.
 */
usb_configuration_descriptor_t * usb_find_configuration_descriptor(
		usb_peripheral_t *device, uint8_t configuration_value);


/**
 * Finds the configuration descriptor associated with the given value.
 *
 * @param configuration_value The value to search for a descriptor for.
 * @return A pointer to the configuration descriptor, or NULL if none could be found.
 */
usb_configuration_descriptor_t * usb_find_other_speed_configuration_descriptor(
		usb_peripheral_t *device, uint8_t configuration_value);

#endif//__USB_H__
