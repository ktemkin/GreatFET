
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
#include "gpio_int.h"




/**
 * Rhododendron packet IDs.
 */
typedef enum {

	// Packets containing raw USB data.
	RHODODENDRON_PACKET_ID_DATA = 0,

	// Packets containing USB events.
	RHODODENDRON_PACKET_ID_RX_START   = 0x80,
	RHODODENDRON_PACKET_ID_RX_END_OK  = 0x81,
	RHODODENDRON_PACKET_ID_RX_END_ERR = 0x82,

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
 * Packet structure for Rhododendron
 */
typedef struct ATTR_PACKED {


} rhododendron_event_packet_t;


/**
 * Forward delcarations.
 */

// Interrupt handlers for DIR going low or high.
static void rhododendron_direction_low_isr(void);
static void rhododendron_direction_high_isr(void);


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
static sgpio_pin_configuration_t ulpi_dir_pin =
	{ .sgpio_pin = 11, .scu_group = 1, .scu_pin =  17, .pull_resistors = SCU_NO_PULL};

static gpio_pin_t ulpi_dir_gpio  = { .port = 0, .pin = 12 };


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
extern uint32_t large_data_buffer[8192];
uint32_t capture_scratch_buffer;


volatile uint32_t usb_buffer_position;

uint32_t capture_buffer_read_position;
volatile uint32_t capture_buffer_write_position;


volatile rhododendron_usb_event_t event_ring[32];
volatile uint32_t event_ring_read_position, event_ring_write_position, events_pending;


const uint8_t rhododendron_direction_isr_priority = 64;


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

	// Set up the direction-capture interrupt.
	// TODO: abstract these interrupt priorities!
	gpio_interrupt_configure(0, ulpi_dir_gpio.port, ulpi_dir_gpio.pin,
		EDGE_SENSITIVE_RISING, rhododendron_direction_high_isr, rhododendron_direction_isr_priority);
	gpio_interrupt_configure(1, ulpi_dir_gpio.port, ulpi_dir_gpio.pin,
		EDGE_SENSITIVE_FALLING, rhododendron_direction_low_isr, rhododendron_direction_isr_priority);


	// Set up the SGPIO functions used for capture...
	rc = sgpio_set_up_functions(&analyzer);
	if (rc) {
		return rc;
	}

	// Turn on our "capture triggered" LED.
	rhododendron_turn_on_led(LED_TRIGGERED);

	// Enable our DIR monitoring ISRs.
	gpio_interrupt_enable(0);
	gpio_interrupt_enable(1);

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
	// Disable our stream-to-host, direction monitor, and SGPIO capture.
	sgpio_halt(&analyzer);
	gpio_interrupt_enable(0);
	gpio_interrupt_enable(1);
	usb_streaming_stop_streaming_to_host();

	// Turn off our "capture triggered" LED.
	rhododendron_turn_off_led(LED_TRIGGERED);
}


/**
 * Consumes a word from our capture buffer, and returns it.
 */
static uint32_t consume_word(void)
{
	uint32_t word = large_data_buffer[capture_buffer_read_position / 4];
	capture_buffer_read_position = (capture_buffer_read_position + 4) % sizeof(large_data_buffer);

	return word;
}


/**
 * Adds a byte to the USB upload buffer.
 */
static void produce_byte(uint8_t byte)
{
	// Add the word to the USB buffer, and move our queue ahead by one word.
	usb_bulk_buffer[usb_buffer_position] = byte;
	usb_buffer_position = (usb_buffer_position + 1) % sizeof(usb_bulk_buffer);
}


/**
 * Adds a word to the USB upload buffer.
 */
static void produce_word(uint32_t word)
{
	uint32_t *usb_buffer = (uint32_t *)usb_bulk_buffer;

	// Add the word to the USB buffer, and move our queue ahead by one word.
	usb_buffer[usb_buffer_position / 4] = word;
	usb_buffer_position = (usb_buffer_position + 4) % sizeof(usb_bulk_buffer);
}


/**
 * Consumes the provided number of words from the capture buffer, and adds them
 * to our USB upload buffer.
 */
static void transfer_words(size_t count)
{
	while(count--) {
		produce_word(consume_word());
	}
}


/**
 * @return True iff the given event would take place during the next event to be processed.
 */
static bool event_in_next_data_chunk(volatile rhododendron_usb_event_t *event)
{
	// For now, we're assuming USB data chunks themselves can't
	// wrap their buffer; as they're currently designed to do so.
	// (Data is always shifted in in little chunks of 32B, so the
	//  capture that causes wrapping would implicitly put the next
	//  chunk at 0.)
	return
		(event->position_in_capture_buffer >= capture_buffer_read_position) &&
		(event->position_in_capture_buffer <  capture_buffer_write_position);
}


/**
 * Emits an event packet to the relevant host.
 */
static void emit_packet_for_event(volatile rhododendron_usb_event_t *event)
{
	// Packet ID: in this case, the event ID is the packet ID.
	produce_byte(event->event_id);

	// Position in the next data packet.
	produce_byte(event->position_in_data_packet);

	// Associated time, in microseconds.
	produce_word(event->time);
}


/**
 * Emit packets to the host for any USB events that have happened recently.
 */
static void emit_pending_event_packets(void)
{

	// Try to process every event we can.
	while(events_pending) {
		volatile rhododendron_usb_event_t *next_event = &event_ring[event_ring_read_position];

		// If the next event isn't in our next data chunk, we'll have to wait for
		// the relevant capture data to appear to process our event. Since events
		// are chronologically ordered, we're done emitting events, for now.
		if (!event_in_next_data_chunk(next_event)) {
			break;
		}

		// Emit a packet for the relevant event...
		emit_packet_for_event(next_event);

		// ... and consume the relevant event.
		--events_pending;
		++event_ring_read_position;
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
	transfer_words(32);
}


/**
 * Core processing thread for Rhododendron. Processes USB data that has come in from
 * the M0 coprocessor; and any events that have come from either the M0 or from IRQs.
 */
void service_rhododendron(void)
{
	// Always emit any event packets relevant to us.
	emit_pending_event_packets();

	// While we have data to consume...
	while(capture_buffer_read_position != capture_buffer_write_position) {

		// ... handle any events that have come in, and any USB data pending.
		emit_usb_data_packet();
		emit_pending_event_packets();
	}
}


/**
 * Adds a USB event to the pending event queue.
 */
static inline void enqueue_pending_usb_event(rhododendron_packet_id_t packet_id)
{
	volatile rhododendron_usb_event_t *event;

	// Immediately capture how many shifts we have remaining, at this point...
	sgpio_shift_position_register_t sgpio_position = analyzer.reg->data_buffer_swap_control[0];

	//  ... the current time, in microseconds...
	uint32_t time = get_time();

	// ... and our position in the capture buffer.
	uint32_t position_in_capture_buffer = capture_buffer_write_position;

	// Grab a write slot in our pending event ring.
	uint32_t write_position = event_ring_write_position;
	event_ring_write_position = (event_ring_write_position + 1) % 32;

	// Get a quick reference to it.
	event = &event_ring[write_position];

	// Finally, populate the event...
	event->event_id = packet_id;
	event->time = time;
	event->position_in_data_packet = sgpio_position.shifts_per_buffer_swap - sgpio_position.shifts_remaining;
	event->position_in_capture_buffer = position_in_capture_buffer;

	// ... and mark it as available for consumption by the other side.
	++events_pending;
}



static void rhododendron_direction_high_isr(void)
{
	GPIO_PIN_INTERRUPT_RISE |= (1 << 0);
	enqueue_pending_usb_event(RHODODENDRON_PACKET_ID_RX_START);
}


static void rhododendron_direction_low_isr(void)
{
	GPIO_PIN_INTERRUPT_FALL |= (1 << 1);
	enqueue_pending_usb_event(RHODODENDRON_PACKET_ID_RX_END_ERR);
}
