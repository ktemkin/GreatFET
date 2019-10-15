
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
 * Interface to the packetization engine -- defined in packetization.c
 */
extern volatile uint32_t packetization_end_of_packets[6];
extern volatile bool new_delineation_data_available;


/**
 * Rhododendron packet IDs.
 */
typedef enum {

	// Packets containing raw USB data.
	RHODODENDRON_PACKET_ID_DATA        = 0,
	RHODODENDRON_PACKET_ID_DELINEATION = 1,

	// Packets containing USB events.
	// TODO

} rhododendron_packet_id_t;


/**
 * Rhododendron pending USB events.
 */
typedef struct rhododendron_usb_event rhododendron_usb_event_t;
struct rhododendron_usb_event {

	// The position in the capture buffer associated with this event.
	// This allows us to queue events, and then add them to the USB stream
	// just before their associated event.
	uint32_t position_in_capture_buffer;

	// The position in the USB data _packet_ associated with this event.
	// This tells us which of the 32 bytes in the USB packet is associated
	// with the relevant event.
	uint32_t position_in_data_packet;

	// The core type of event this is.
	rhododendron_packet_id_t event_id;

	// The system time associated with the relevent event.
	uint32_t time;
};



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


static gpio_pin_t ulpi_dir_gpio      = { .port = 0, .pin = 12 };
static gpio_pin_t ulpi_nxt_alt_gpio  = { .port = 2, .pin = 15 };


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
 * Capture-state variables.
 */
volatile uint32_t usb_buffer_position;

volatile uint32_t capture_buffer_read_position = 0;
volatile uint32_t capture_buffer_write_position = 0;

volatile rhododendron_usb_event_t event_ring[128];
volatile uint32_t event_ring_read_position, event_ring_write_position, events_pending;


const uint8_t rhododendron_direction_isr_priority = 64;


// Buffer allocated for large data processing.
// Currently shared. Possibly should be replaced with malloc'd buffers?
uint8_t capture_buffer[8192] ATTR_SECTION(".bss.heap");



uint32_t xxx_total_bytes_produced;


/**
 * Starts a Rhododendron capture of high-speed USB data.
 */
int rhododendron_start_capture(void)
{
	int rc;

	// Start from the beginning of our buffers.
	usb_buffer_position = 0;
	capture_buffer_read_position = 0;
	capture_buffer_write_position = 0;

	// Clear any pending events.
	event_ring_write_position = 0;
	event_ring_write_position = 0;
	events_pending = 0;

	// Set up the SGPIO functions used for capture...
	rc = sgpio_set_up_functions(&analyzer);
	if (rc) {
		return rc;
	}

	// ... set up the packetization driver ...
	rhododendron_start_packetization();

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

	// Disable our stream-to-host, direction monitor, and SGPIO capture.
	sgpio_halt(&analyzer);
	usb_streaming_stop_streaming_to_host();

	rhododendron_stop_packetization();

	// Turn off our "capture triggered" LED.
	rhododendron_turn_off_led(LED_TRIGGERED);

	pr_info("Position in USB buffer: %08x\n", usb_buffer_position);
}


/**
 * Consumes a word from our capture buffer, and returns it.
 */

/**
 * Consumes a word from our capture buffer, and returns it.
 */
static uint32_t consume_byte(void)
{
	uint8_t byte = capture_buffer[capture_buffer_read_position];
	capture_buffer_read_position = (capture_buffer_read_position + 1) % sizeof(capture_buffer);

	return byte;
}


/**
 * Adds a byte to the USB upload buffer.
 */
static void produce_byte(uint8_t byte)
{
	// Add the word to the USB buffer, and move our queue ahead by one word.
	usb_bulk_buffer[usb_buffer_position] = byte;
	usb_buffer_position = (usb_buffer_position + 1) % 32768;
}


/**
 * Adds a 32-bit word to the USB upload buffer.
 */
static void produce_word(uint32_t word)
{
	uint8_t *as_bytes = (uint8_t *)&word;

	for (unsigned i = 0; i < sizeof(word); ++i) {
		produce_byte(as_bytes[i]);
	}
}





/**
 * Adds a 16-bit word to the USB upload buffer.
 */
static void produce_halfword(uint16_t halfword)
{
	uint8_t *as_bytes = (uint8_t *)&halfword;

	for (unsigned i = 0; i < sizeof(halfword); ++i) {
		produce_byte(as_bytes[i]);
	}
}


/**
 * Consumes the provided number of words from the capture buffer, and adds them
 * to our USB upload buffer.
 */
static void transfer_bytes(size_t count)
{
	while(count--) {
		produce_byte(consume_byte());
	}
}



/**
 * Emits processed-and-packetized USB data to our host for proessing.
 */
static void emit_usb_data_packet(void)
{
	//
	// TODO: process this data more actively; allowing for e.g. filtering
	//

	// Produce our packet header...
	produce_byte(RHODODENDRON_PACKET_ID_DATA);

	// ... and then transfer a full buffer's worth of slices.
	transfer_bytes(32);
}


void emit_packet_delineations(void)
{
	// If we don't have any new delineations, we're done!
	if (!new_delineation_data_available) {
		return;
	}

	//
	// Otherwise, we'll need to emit them to the host.
	//

	// Add our packet header..
	produce_byte(RHODODENDRON_PACKET_ID_DELINEATION);

	// ... and each of our packet boundaries.
	for (unsigned i = 0; i < ARRAY_SIZE(packetization_end_of_packets); ++i) {
		produce_halfword(packetization_end_of_packets[i]);
	}

	// Finally, mark the data as processed.
	new_delineation_data_available = false;
}

/**
 * Returns the data buffer capture count.
 * Assumes the data buffer never fills or overflows.
 */
static uint32_t capture_buffer_data_count(uint32_t write_position)
{
	uint32_t virtual_write_pointer = write_position;
	uint32_t virtual_read_pointer  = capture_buffer_read_position;

	// If the capture buffer write position is _before_ the capture buffer read
	// position, then we're wrapping around the buffer's end. We'll account for
	// this by undoing the most recent modulus -- the one that caused the wrap-around.
	if (virtual_write_pointer < virtual_read_pointer) {
		virtual_write_pointer += sizeof(capture_buffer);
	}

	return virtual_write_pointer - virtual_read_pointer;
}

/**
 * Core processing thread for Rhododendron. Processes USB data that has come in from
 * the M0 coprocessor; and any events that have come from either the M0 or from IRQs.
 */
void service_rhododendron(void)
{
	// Store a reference to the current write position, so we don't
	// keep reading it and blocking the M0 from accessing the bus.
	uint32_t write_position = capture_buffer_write_position;

	if (!capture_active) {
		return;
	}

	// While we have data to consume...
	while (capture_buffer_data_count(write_position)) {
		emit_usb_data_packet();
		emit_packet_delineations();
	}
}
