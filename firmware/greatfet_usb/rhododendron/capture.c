
/*
 * This file is part of GreatFET
 *
 * Code for ULPI register interfacing via SGPIO for Rhododendron boards.
 */


#include <debug.h>
#include <errno.h>
#include <string.h>

#include <drivers/gpio.h>
#include <drivers/sgpio.h>
#include <drivers/platform_clock.h>
#include <drivers/platform_reset.h>
#include <drivers/scu.h>
#include <drivers/timer.h>
#include <drivers/arm_vectors.h>
#include <drivers/memory/allocator.h>
#include <toolchain.h>

#include "../pin_manager.h"
#include "../rhododendron.h"
#include "../usb_streaming.h"
#include "gpio_int.h"

bool capture_active = false;

/**
 * ULPI data pins for Rhododendron boards.
 */
static sgpio_pin_configuration_t ulpi_data_pins[] = {
	{ .sgpio_pin = 0,  .scu_group = 0, .scu_pin =  0, .pull_resistors = SCU_PULLDOWN},
	{ .sgpio_pin = 1,  .scu_group = 0, .scu_pin =  1, .pull_resistors = SCU_PULLDOWN},
	{ .sgpio_pin = 2,  .scu_group = 1, .scu_pin = 15, .pull_resistors = SCU_PULLDOWN},
	{ .sgpio_pin = 3,  .scu_group = 1, .scu_pin = 16, .pull_resistors = SCU_PULLDOWN},
	{ .sgpio_pin = 4,  .scu_group = 6, .scu_pin =  3, .pull_resistors = SCU_PULLDOWN},
	{ .sgpio_pin = 5,  .scu_group = 6, .scu_pin =  6, .pull_resistors = SCU_PULLDOWN},
	{ .sgpio_pin = 6,  .scu_group = 2, .scu_pin =  2, .pull_resistors = SCU_PULLDOWN},
	{ .sgpio_pin = 7,  .scu_group = 6, .scu_pin =  8, .pull_resistors = SCU_PULLDOWN},
};


/**
 * ULPI control pins (as used here).
 */
static sgpio_pin_configuration_t ulpi_nxt_pin =
	{ .sgpio_pin = 10, .scu_group = 1, .scu_pin =  14, .pull_resistors = SCU_NO_PULL};


/**
 * Core function to capture USB data.
 */
sgpio_function_t usb_capture_functions[] = {
	{
		.enabled                     = true,

		// Once we get to this point, we're just observing the USB data as it flies by.
		.mode                        = SGPIO_MODE_STREAM_DATA_IN,

		// We're interesting in reading data from the PHY data pins.
		.pin_configurations          = ulpi_data_pins,
		.bus_width                   = ARRAY_SIZE(ulpi_data_pins),


		// We'll shift in time with rising edges of the PHY clock.
		.shift_clock_source          = SGPIO_CLOCK_SOURCE_COUNTER,
		.shift_clock_edge            = SGPIO_CLOCK_EDGE_RISING,
		.shift_clock_frequency       = 0, // Never divide; just use the SGPIO clock frequency.


		// We're only interested in values that the PHY indicates are valid data.
		.shift_clock_qualifier       = SGPIO_QUALIFIER_SGPIO10,
		.shift_clock_qualifier_input = &ulpi_nxt_pin,
		.shift_clock_qualifier_is_active_low = false,

		// Capture our data into the USB bulk buffer, all ready to be sent up to the host.
		.buffer                      = usb_bulk_buffer,
		.buffer_order                = 15,              // 2 ^ 15 == 32768 == sizeof(usb_bulk_buffer)

		// Capture an unlimited amount of data.
		.shift_count_limit            = 0,
	},
};


/**
 * Core USB capture SGPIO configuration.
 */
sgpio_t analyzer  = {
	.functions      = usb_capture_functions,
	.function_count = ARRAY_SIZE(usb_capture_functions),
};


/**
 * Capture-state variables.
 */
volatile uint32_t usb_buffer_position;


/**
 * Starts a Rhododendron capture of high-speed USB data.
 */
int rhododendron_start_capture(void)
{
	int rc;

	// Start from the beginning of our buffers.
	usb_buffer_position = 0;

	// Set up the SGPIO functions used for capture...
	rc = sgpio_set_up_functions(&analyzer);
	if (rc) {
		return rc;
	}

	// ... set up the packetization driver ...
	//rhododendron_start_packetization();

	// ... turn on our "capture triggered" LED...
	rhododendron_turn_on_led(LED_TRIGGERED);

	// FIXME: verify that the Rhododendron loadable is there?
	capture_active = true;

	// ... and enable USB streaming to the host.
	usb_streaming_start_streaming_to_host(
		(uint32_t *volatile)&usb_buffer_position,
		NULL);
	sgpio_run(&analyzer);

	return 0;
}


/**
 * Terminates a Rhododendron capture.
 */
void rhododendron_stop_capture(void)
{
	capture_active = false;

	//rhododendron_stop_packetization();

	// Disable our stream-to-host, direction monitor, and SGPIO capture.
	sgpio_halt(&analyzer);
	usb_streaming_stop_streaming_to_host();

	// Turn off our "capture triggered" LED.
	rhododendron_turn_off_led(LED_TRIGGERED);

	pr_info("Position in USB buffer: %08x\n", usb_buffer_position);
}

