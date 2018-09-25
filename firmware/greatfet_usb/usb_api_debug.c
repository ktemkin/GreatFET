/*
 * This file is part of GreatFET
 */

#include <stddef.h>
#include <greatfet_core.h>
#include <debug.h>

#include <drivers/comms.h>

#define CLASS_NUMBER_DEBUG (0x1234)


/**
 * Command to read the contents of the debug ring buffer.
 */
static int verb_read_dmesg(struct command_transaction *trans)
{
	trans->data_out_length = debug_ring_read(trans->data_out, trans->data_out_max_length, false);
	return 0;
}

/**
 * Commmand that clears the debug ring.
 */
static int verb_clear_dmesg(struct command_transaction *trans)
{
	// Perform a read where we discard the results.
	trans->data_out_length = debug_ring_read(trans->data_out, trans->data_out_max_length, false);
	return 0;
}


/**
 * Verbs for the debug API.
 */
struct comms_verb debug_verbs[] = {
		{ .verb_number = 0x0, .handler = verb_read_dmesg },
		{ .verb_number = 0x1, .handler = verb_clear_dmesg },
		{} // Sentinel
};
COMMS_DEFINE_SIMPLE_CLASS(debug_api, CLASS_NUMBER_DEBUG, "Debug API", debug_verbs);
