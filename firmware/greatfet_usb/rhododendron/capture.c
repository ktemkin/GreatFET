
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
#include <toolchain.h>

#include "../pin_manager.h"
#include "../rhododendron.h"
#include "../usb_streaming.h"

// XXX DEBUG ONLY
#include <libopencm3/lpc43xx/ipc.h>


extern uint8_t large_data_buffer[16384];

volatile uint32_t usb_buffer_position;

volatile uint32_t capture_buffer_write_position;
uint32_t capture_buffer_read_position;


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
	{ .sgpio_pin = 10, .scu_group = 1, .scu_pin =  14, .pull_resistors = SCU_PULLDOWN};


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


#ifdef RHODODENDRON_USE_USB1_CLK_AS_ULPI_CLOCK

		// We'll shift in time with rising edges of the PHY clock.
		.shift_clock_source          = SGPIO_CLOCK_SOURCE_COUNTER,
		.shift_clock_edge            = SGPIO_CLOCK_EDGE_RISING,
		.shift_clock_frequency       = 0, // Never divide; just use the SGPIO clock frequency.


#else

		// We'll shift in time with rising edges of the PHY clock.
		.shift_clock_source          = SGPIO_CLOCK_SOURCE_SGPIO08,
		.shift_clock_edge            = SGPIO_CLOCK_EDGE_RISING,
		.shift_clock_input           = &ulpi_clk_pin,

#endif

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
 * Starts a Rhododendron capture of high-speed USB data.
 */
int rhododendron_start_capture(void)
{
	extern uint32_t m0_vector_table;
	extern uint8_t sgpio_m0_code, sgpio_m0_code_end;

	extern volatile uint32_t usb_buffer_position;

	int rc;

	// Start from the beginning of our buffers.
	usb_buffer_position = 0;
	capture_buffer_read_position = 0;
	capture_buffer_write_position = 0;


	// Set up the SGPIO functions used for capture...
	rc = sgpio_set_up_functions(&analyzer);
	if (rc) {
		return rc;
	}

	// Turn on our "capture triggered" LED.
	rhododendron_turn_on_led(LED_TRIGGERED);

	// FIXME: verify that the Rhododendron loadable is there?

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
	// Disable our stream-to-host, and disable the SGPIO capture.
	sgpio_halt(&analyzer);
	usb_streaming_stop_streaming_to_host();

	// Turn off our "capture triggered" LED.
	rhododendron_turn_off_led(LED_TRIGGERED);
}


static inline void consume_data(size_t amount)
{
	capture_buffer_read_position = (usb_buffer_position + amount) % sizeof(large_data_buffer);
}

static inline void produce_data(void *data, size_t length)
{
	uint32_t *usb_buffer = (uint32_t *)usb_bulk_buffer;
	uint32_t *data_buffer = (uint32_t *)data;

	while(length) {
		usb_buffer[usb_buffer_position / 4] = *data_buffer;
		usb_buffer_position = (usb_buffer_position + 4) % sizeof(usb_bulk_buffer);

		data_buffer++;
		length -= 4;
	}
}


void service_rhododendron(void)
{
	while(capture_buffer_read_position != capture_buffer_write_position) {

		uint32_t packet_type = large_data_buffer[capture_buffer_read_position / 4];

		switch (packet_type) {

		}

	}
}
