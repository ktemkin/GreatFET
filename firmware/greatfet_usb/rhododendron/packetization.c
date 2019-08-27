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


// Get a reference to our SCT registers.
static volatile platform_sct_register_block_t *reg = (void *)0x40000000;


/**
 * Nice, constant names for the SCT pins for NXT and DIR.
 */
typedef enum {
	IO_PIN_CLK = 2,
	IO_PIN_NXT = 3,
	IO_PIN_DIR = 5
} io_pin_t;


/**
 * Buffer that holds any active packet-boundary information.
 *  - Produced by our packetization interrupt.
 *  - Consumed by the main capture code.
 */
volatile uint32_t packetization_end_of_packets[14];
volatile bool new_delineation_data_available = false;


// Forward declarations.
static void packetization_isr(void);


/**
 * Configure the SCT I/O pins in the SCU to be routed to the SCT.
 */
static void configure_io(void)
{
	// Configure each of our three pins to tie to the SCT.
	platform_scu_configure_pin_fast_io(2, 5, 1, SCU_NO_PULL); // CLK
	platform_scu_configure_pin_fast_io(1, 0, 3, SCU_NO_PULL); // NXT
	platform_scu_configure_pin_fast_io(1, 6, 1, SCU_NO_PULL); // DIR
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
	reg->clock_mode = 0; //SCT_COUNT_ON_INPUT;
	reg->clock_on_falling_edges = false;
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

	// By default, don't count. Our SCT will begin counting once it detects a start-of-packet.
	reg->control_low.pause_counter = true;

	// We always want to count up; so we'll wrap around on overflow. The listening software
	// should be able to detect this overflow condition and handle things.
	reg->control_low.counter_switches_direction_on_overflow = false;

	// We'll count bytes, so we'll apply a prescaler of 8
	reg->control_low.count_prescalar = 8 - 1;

}


/**
 * Configure one of our count events, which don't affect state change, and only drive our counter behaviors.
 * These occur in response to a change in the state of NXT.
 */
static void configure_count_event(uint8_t event_number, io_condition_t condition)
{
	reg->event[event_number].condition               = ON_IO;
	reg->event[event_number].associated_io_condition = condition;
	reg->event[event_number].associated_io_pin       = IO_PIN_NXT;
	reg->event[event_number].enabled_in_state        = -1; // All states.
	reg->event[event_number].controls_output         = false;
	reg->event[event_number].load_state              = false;
	reg->event[event_number].next_state              = 0;
}


/**
 * Configure one of our capture events, which actually capture our packet boundaries.
 * These occur each time a packet ends (when DIR drops to 0).
 */
static void configure_capture_event(uint8_t event_number, uint8_t current_state, uint8_t next_state)
{
	reg->event[event_number].condition               = ON_IO;
	reg->event[event_number].associated_io_condition = IO_CONDITION_FALL;
	reg->event[event_number].associated_io_pin       = IO_PIN_DIR;
	reg->event[event_number].enabled_in_state        = (1 << current_state);
	reg->event[event_number].controls_output         = false;
	reg->event[event_number].load_state              = true;
	reg->event[event_number].next_state              = next_state;
}


/**
 * Configures all of the relevant SCT events.
 *
 * We'll use the SCT and some simple event rules to track bit edges. These create a simple FSM, but they're
 * easy to describe as simple rules, here.
 *
 * Events:
 *    0    -- a rising edge of NXT has occurred; so we'll start counting ULPI clock edges
 *    1    -- a falling edge of NXT has occurred; we'll stop counting
 *    2-14 -- a falling edge of DIR has occurred, so we've finished a packet -- capture the count into count[event-2]
 *    15   -- same as 2-14, but we've captured enough data that we want to signal an interrupt
 *
 * Events 2-15 activate in order, in order to capture a sequential series of packet lengths / count values.
 * To keep these separate, we use a state variable to track which counter value we're currently capturing to.
 *
 * Our state counts from up on events 2-14, and then resets back to zero after event 15; accordinly, we only use
 * counters 0-13 [14 events].
 */
static void configure_events(void)
{
	int state;

	// We never want to clear the counter, so don't clear it on any events.
	reg->clear_counter_on_event.all = 0;

	// We don't want to halt the SCT on any events, either.
	reg->halt_on_event.all = 0;

	//
	// Set up our NXT-tracking events, which are enabled in all states:
	//

	// Event 0 triggers us to start counting when NXT goes high.
	configure_count_event(0, IO_CONDITION_RISE);
	reg->start_on_event.all = (1 << 0);
	reg->start_on_event.all = 0; // XXX

	// Event 1 triggers us to stop counting when NXT goes low.
	configure_count_event(1, IO_CONDITION_FALL);
	reg->stop_on_event.all = (1 << 1);

	//
	// Configure each of our capture events.
	//

	// We'll capture whenever a packet ends (events 2-15).
	state = 0;
	reg->capture_on_event.all = 0;
	for (int event = 2; event <= 15; ++event) {

		// We'll configure our FSM to wrap back around after we reach state 0.
		bool wraps_around = (state == 15);

		// Configure each of the events to only occur on the state associated with the
		// counter they're going to capture into, and to move to the next state.
		configure_capture_event(event, state, wraps_around ? 0 : state + 1);

		// Configure each of theses events to trigger a capture, and trigger each capture register
		// to capture on their relevant event.
		reg->capture_on_event.all         |= (1 << event);
		reg->captures_on_event[state].all =  (1 << event);

		// Also, stop counting whenever DIR drops low.
		reg->stop_on_event.all            |= (1 << event);

		// Move to configuring the next state.
		++state;
	}

	// We'll trigger the CPU to collect our collected end-of-packets once we've captured all 14 we can handle.
	reg->interrupt_on_event = (1 << 15);
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
	// Mark the interrupt as serviced by clearing the "event occurred" flag for our final capture event (event 15)...
	reg->event_occurred = (1 << 15);

	// ... buffer all of the packet capture data...
	for (unsigned i = 0; i < ARRAY_SIZE(packetization_end_of_packets); ++i) {
		packetization_end_of_packets[i] = reg->capture[i].all;
	}

	// ... and indicate to our main capture code that delineation data is ready.
	new_delineation_data_available = true;
}


/**
 * Starts the core Rhododendron packetization engine, which populates the packetization_end_of_packets
 * array using our State Configurable Timer to detect packet edges.
 */
void rhododendron_start_packetization(void)
{
	// Set up our core packetization engine.
	set_up_packetization();

	// Ensure the counter isn't running at the start, and ensure no events can occur.
	reg->control.halt_sct = true;
	reg->control.pause_counter = true;

	// Start off with a counter value of zero.
	reg->control_low.clear_counter_value = 1;

	// Start off in an initial state of 0.
	reg->state = 0;

	pr_info("initial count: %d\n", reg->count);

	// Finally, enable events to start the counter.
	reg->control.halt_sct = false;

	uint32_t *config = (void *)reg;
	pr_info("SCT config: %08x / %08x\n", *config, reg->control_low);
}


/**
 * Halts the core Rhododendron packetization engine.
 */
void rhododendron_stop_packetization(void)
{
	platform_disable_interrupt(SCT_IRQ);
	reg->control.halt_sct = false;
}


/**
 * Debug function.
 */
uint32_t rhododendron_get_byte_counter(void)
{
	return reg->count;
}
