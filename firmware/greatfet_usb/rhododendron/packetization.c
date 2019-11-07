/**
 * Packetization engine for Rhododendron.
 */

#include <debug.h>
#include <errno.h>
#include <toolchain.h>

#include <drivers/sct.h>
#include <drivers/scu.h>
#include <drivers/arm_vectors.h>
#include <drivers/platform_clock.h>
#include <drivers/platform_config.h>

// XXX: HACK:
#define LIBOPENCM3_VECTOR_H
#include <drivers/usb/usb.h>
#include <drivers/usb/usb_queue.h>

#include "../usb_endpoint.h"

// Get a reference to our SCT registers.
static volatile platform_sct_register_block_t *reg = (void *)0x40000000;


/**
 * Nice, constant names for the SCT pins for NXT and DIR.
 */
typedef enum {
	IO_PIN_CLK = 2,
	IO_PIN_NXT = 3,
	IO_PIN_DIR = 5,
} io_pin_t;


/**
 * Buffer that holds any active packet-boundary information.
 *  - Produced by our packetization interrupt.
 *  - Consumed by the main capture code.
 */
static const uint32_t boundaries_per_packet = 6;
static const uint8_t maximum_event = 10;


/**
 * Buffer that stores delineation data as we wait for the host to grab it.
 */
volatile uint32_t delineation_data[768];
volatile uint32_t position_in_delineation_buffer;


// Forward declarations.
static void packetization_isr(void);


/**
 * Configure the SCT I/O pins in the SCU to be routed to the SCT.
 */
static void configure_io(void)
{
	// Configure each of our three pins to tie to the SCT.
	platform_scu_configure_pin_fast_io(2, 5, 1, SCU_NO_PULL); // CLK
	platform_scu_configure_pin_fast_io(1, 0, 1, SCU_PULLDOWN); // NXT
	platform_scu_configure_pin_gpio(1, 6, 1, SCU_PULLDOWN); // DIR
}


static void configure_clocking(void)
{
	// TODO: enable the relevant clock, rather than assuming it's enabled
	platform_clock_control_register_block_t *ccu = get_platform_clock_control_registers();
	platform_enable_branch_clock(&ccu->m4.sct, false);
}


/**
* Performs high-level SCT configuration for our packetization counter.
*/
static void configure_sct(void)
{
	// Use both halves of the counter as one unified counter. We don't technically need
	// the precision; but for now, we're using all of the possible SCT event numbers, so we
	// might as well take advantage of the otherwise wasted other half.
	reg->use_both_halves_as_one = true;

	// We'll increment our counter in time with the ULPI clock; but we'll still run the SCT
	// off of our main system clock.
	reg->clock_mode = SCT_COUNT_ON_INPUT;
	reg->clock_on_falling_edges = true;
	reg->clock_input_number = IO_PIN_CLK;

	// The inputs we're interested in are synchronized to the ULPI clock rather than the SCT one; s
	// so we'll synchronize them before processing them.
	reg->synchronize_input_2 = true; // CLK
	reg->synchronize_input_3 = true; // NXT
	reg->synchronize_input_5 = true; // DIR
}


/**
 * Sets up the SCT's counter to count bits.
 */
static void set_up_bit_counter(void)
{
	// Start off with the entire SCT disabled, so we don't process any actions.
	// We'll change this later with rhododendron_start_packetization().
	reg->control_low.halt_sct = true;

	// The counter should always increment, so we're actively counting the number of bits.
	reg->control_low.counter_should_count_down = false;

	// We always want to count up; so we'll wrap around on overflow. The listening software
	// should be able to detect this overflow condition and handle things.
	reg->control_low.counter_switches_direction_on_overflow = false;

	// We'll count bytes, and this is a parallel bus, so we'll just set the prescaler to '0'.
	reg->control_low.count_prescaler = 0;
}




/**
 */
static void configure_noncapturing_event(uint32_t event_number, uint32_t state_mask,
	uint32_t next_state_delta,  io_pin_t pin, io_condition_t condition)
{
	reg->event[event_number].condition               = ON_IO;
	reg->event[event_number].associated_io_condition = condition;
	reg->event[event_number].associated_io_pin       = pin;
	reg->event[event_number].enabled_in_state        = state_mask;
	reg->event[event_number].controls_output         = false;
	reg->event[event_number].load_state              = false;
	reg->event[event_number].next_state              = next_state_delta;
}


/**
 * Configure one of our capture events, which actually capture our packet boundaries.
 * These occur each time a packet ends (when DIR drops to 0, after NXT has become 0).
 */
static void configure_capturing_event(uint32_t event_number, uint32_t current_state, uint32_t next_state)
{
	reg->event[event_number].condition               = ON_IO;
	reg->event[event_number].associated_io_condition = IO_CONDITION_LOW;
	reg->event[event_number].associated_io_pin       = IO_PIN_DIR;
	reg->event[event_number].enabled_in_state        = (1 << current_state);
	reg->event[event_number].controls_output         = false;
	reg->event[event_number].load_state              = true;
	reg->event[event_number].next_state              = next_state;
}


/**
 * Configures all of the relevant SCT events, building our FSM.
 */
static void configure_events(void)
{
	// This mask represents all of the states that are equivalent to "state 0" in our FSM.
	// It can be shifted to the left once to get the mirrors of state 1, or twice to get the mirrors of state 2.
	unsigned state_mirror_mask = 0;
	unsigned max_state = 5;

	if ((max_state * max_state) >= (sizeof(state_mirror_mask) * 8)) {
		pr_error("Proposed state_mirror_mask with max_state %d cannot fit!\n", max_state);
	}

	for (unsigned i = 0; i <= max_state; ++i) {
		state_mirror_mask |= (1 << (i * max_state));
	}

	/*const unsigned state_mirror_mask =*/
			/*(1 <<  0) | (1 <<  5) | (1 << 10) | (1 << 15) |*/
			/*(1 << 20) | (1 << 25);*/

	unsigned state, capture_register;

	// We never want to clear the counter, so don't clear it on any events.
	reg->clear_counter_on_event.all = 0;

	// We don't want to halt the SCT on any events, either.
	reg->halt_on_event.all = 0;

	// State 0 (and mirrors): waiting for NXT to go high.
	// We move to state '1' when NXT goes high, and stay in the same place otherwise.
	configure_noncapturing_event(0, state_mirror_mask << 0, 1, IO_PIN_NXT, IO_CONDITION_HIGH);
	reg->start_on_event.all = (1 << 0);


	// State 1 (and mirrors): waiting for NXT to go low.
	// We move to state '2' when NXT goes low, and stay in the same place otherwise.
	configure_noncapturing_event(1, state_mirror_mask << 1, 1, IO_PIN_NXT, IO_CONDITION_LOW);
	reg->stop_on_event.all = (1 << 1);


	// State 2 (and mirrors): wait state; we wait for the next RE of the ULPI CLK
	configure_noncapturing_event(2, state_mirror_mask << 2, 1, IO_PIN_CLK, IO_CONDITION_RISE);
	configure_noncapturing_event(3, state_mirror_mask << 3, 1, IO_PIN_CLK, IO_CONDITION_LOW);

	// State 3 (and mirrors): we figure out if DIR has also gone low (and thus if we've just ended a packet)
	// If DIR is still high, we move back to state '0'.
	configure_noncapturing_event(4, state_mirror_mask << 4, 32 - 4, IO_PIN_DIR, IO_CONDITION_HIGH);

	// If DIR is now low, we've ended a packet, and we're ready to capture.
	// We'll capture whenever a packet ends (events 3-11).
	state = 4;
	capture_register = 0;
	reg->use_register_for_capture.all = 0;
	for (int event = 5; event <= maximum_event; ++event) {

		// We'll configure our FSM to wrap back around after we reach our maximum state.
		bool wraps_around = (event == maximum_event);

		// Configure each of the events to only occur on the state associated with the
		// counter they're going to capture into, and to move to the next state.
		configure_capturing_event(event, state, wraps_around ? 0 : state + 1);

		// Configure each of theses events to trigger a capture, and trigger each capture register
		// to capture on their relevant event.
		reg->use_register_for_capture.all            |= (1 << capture_register);
		reg->capture_on_events[capture_register].all  = (1 << event);

		// Move to configuring the next mirror of state '2'...
		state += 5;
		++capture_register;
	}

	// We'll trigger the CPU to collect our collected end-of-packets once we've captured all we can handle.
	reg->interrupt_on_event = (1 << maximum_event);
}


/**
 * Sets up the ISR that will capture packet boundaries.
 */
static void set_up_isr()
{
	// Ensure that no events are pending.
	reg->event_occurred = 0xFF;

	// Install and enable our interrupt.
	platform_disable_interrupt(SCT_IRQ);
	platform_set_interrupt_priority(SCT_IRQ, 0);
	platform_set_interrupt_handler(SCT_IRQ, packetization_isr);
	platform_enable_interrupt(SCT_IRQ);
}


/**
 * Configure the system to automatically detect the bit numbers for end-of-packet events;
 * which we'll use to break our USB data stream into packets.
 */
static void set_up_packetization(void)
{
	configure_io();
	configure_clocking();
	configure_sct();
	set_up_bit_counter();
	configure_events();
	set_up_isr();
}


/**
 * Core packetization ISR -- occurs when we've captured a full set of "end of packet" markers, and area
 * for the main capture routine to emit them to the host.
 */
static void packetization_isr(void)
{
	volatile uint32_t *delineation_buffer = &delineation_data[position_in_delineation_buffer];
	const uint32_t half_buffer_size = ARRAY_SIZE(delineation_data) / 2;

	// Mark the interrupt as serviced by clearing the "event occurred" flag for our final capture event (event 12)...
	reg->event_occurred = (1 << maximum_event);

	// ... buffer all of the packet capture data...
	for (unsigned i = 0; i < boundaries_per_packet; ++i) {
		delineation_buffer[i] = reg->capture[i].all;
	}

	// ... move our position in the buffer forward...
	position_in_delineation_buffer =
			(position_in_delineation_buffer + boundaries_per_packet) % ARRAY_SIZE(delineation_data);


	// If we've just crossed a half-buffer boundary, send our packetization data to the host.
	if ((position_in_delineation_buffer % half_buffer_size) == 0) {

		// Always send the half we've just completed, which is located in the half of the buffer
		// opposite our normal position.
		uint32_t position_to_send =
			(position_in_delineation_buffer + half_buffer_size) % ARRAY_SIZE(delineation_data);

		usb_transfer_schedule(
			&usb0_endpoint_delineation,
			(void *)&delineation_data[position_to_send],
			half_buffer_size, 0, 0);
	}
}

/**
 * Starts or stops SCT operation.
 */
static void set_counter_running(bool running)
{
	sct_control_register_t control_value = reg->control;

	// We'll always pause the counter on our initial comms; it'll be enabled by our events.
	// It doesn't hurt to pause it when we finally stop, either; even though it won't count with halt true.
	control_value.pause_counter = true;
	control_value.halt_sct = !running;

	// Set the value of pause and halt at the same time. This is necessary, as the SCT hardware automatically
	// clears the "pause" register when the SCT is halted.
	reg->control = control_value;
}


/**
 * Starts the core Rhododendron packetization engine, which populates the packetization_end_of_packets
 * array using our State Configurable Timer to detect packet edges.
 */
void rhododendron_start_packetization(void)
{
	// Set up the USB endpoint we use to transmit sideband data.
	usb_endpoint_init(&usb0_endpoint_delineation);

	// Set up our core packetization engine.
	set_up_packetization();

	// Ensure the counter isn't running at the start, and ensure no events can occur.
	reg->control.halt_sct = true;

	// Start off with a counter value of zero.
	reg->control_low.clear_counter_value = 1;

	// Start off in an initial state of 0.
	reg->state = 0;

	// Start off at the first position in our delneation buffer.
	position_in_delineation_buffer = 0;

	// Finally, enable events to start the counter, and start with the counter paused.
	set_counter_running(true);
}


/**
 * Halts the core Rhododendron packetization engine.
 */
void rhododendron_stop_packetization(void)
{
	usb_endpoint_disable(&usb0_endpoint_delineation);

	platform_disable_interrupt(SCT_IRQ);
	set_counter_running(false);

	pr_info("Packetization terminated with count %u in state %u.\n", reg->count, reg->state);

	for (unsigned i=0; i < ARRAY_SIZE(delineation_data); ++i) {
		/*pr_info("delineator[%u] = %u\n", i, delineation_data[i]);*/
	}
}


/**
 * Debug function.
 */
uint32_t rhododendron_get_byte_counter(void)
{
	return reg->count;
}
