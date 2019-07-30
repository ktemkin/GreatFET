/*
 * This file is part of GreatFET
 */

#include <drivers/comms.h>
#include <debug.h>

#include <stddef.h>
#include <errno.h>
#include <ctype.h>

#include <drivers/uart.h>

#define CLASS_NUMBER_SELF (0x112)

// TODO: abstract the UART count
static uart_t uart[4];

static int verb_initialize(struct command_transaction *trans)
{
	// TODO: actually implement this
	uint8_t uart_number = comms_argument_parse_uint8_t(trans);
	uart[uart_number].baud_rate = comms_argument_parse_uint32_t(trans);
	uart[uart_number].data_bits = 8;
	uart[uart_number].parity_mode = NO_PARITY;
	uart[uart_number].stop_bits = ONE_STOP_BIT;
	uart[uart_number].number = 0;

	pr_info("baud rate is %" PRIu32 ". have a nice day.\n", uart[0].baud_rate);
	uart_init(&uart[uart_number]);

	comms_response_add_uint32_t(trans, 23);
	return 0;
}


static struct comms_verb _verbs[] = {
		{ .name = "initialize", .handler = verb_initialize,
			.in_signature = "<BIBBB", .out_signature = "<I",
			.in_param_names = "uart_number, baud_rate, data_bits, parity_mode, stop_bits",
			.out_param_names = "baud_achieved", .doc =
			"Prepares a UART for use by the rest of this API.\n"
			"\n"
			"Parameters:\n"
			"    uart_number -- The number of the UART to use.\n"
			"    baud_rate -- The desired baud rate for comms.\n" // TODO: or 0 to autobaud
			"    data_bits -- The number of data bits per frame.\n"
			"    parity mode -- The parity mode to use (0 = none, 1 = odd, 2 = even, 3 = always one, 4 = always zero).\n"
			"    stop_bits -- The number of stop bits.\n"
			"Returns the actual baud rate achieved, in Hz."
		},
		{}
};
COMMS_DEFINE_SIMPLE_CLASS(uart, CLASS_NUMBER_SELF, "uart", _verbs,
        "functions to enable talking 'serial'")

