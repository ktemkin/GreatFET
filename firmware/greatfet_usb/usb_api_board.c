/*
 * This file is part of GreatFET
 */

#include <stddef.h>
#include <string.h>
#include <errno.h>

#include <greatfet_core.h>
#include <rom_iap.h>
#include <libopencm3/lpc43xx/wwdt.h>

#include <drivers/comms.h>

#define CLASS_NUMBER_CORE 0

char version_string[] = VERSION_STRING;

static int verb_read_board_id(struct command_transaction *trans)
{
	comms_respond_uint32_t(trans, BOARD_ID);
	return 0;
}


static int verb_read_version_string(struct command_transaction *trans)
{
	comms_respond_string(trans, version_string);
	return 0;
}


static int verb_read_part_id(struct command_transaction *trans)
{
	void *position;
	iap_cmd_res_t iap_cmd_res;

	// Don't allow a read if we can't fit a full response.
	if (trans->data_out_max_length < 8)
		return EINVAL;

	// Read the IAP part number...
	iap_cmd_res.cmd_param.command_code = IAP_CMD_READ_PART_ID_NO;
	iap_cmd_call(&iap_cmd_res);

	// ... and build our response from it.
	position = comms_start_response(trans);
	for (int i = 0; i < 2; ++i)
		comms_response_add_uint32_t(trans, &position, iap_cmd_res.status_res.iap_result[i]);

	// Return whether our data is valid.
	return iap_cmd_res.status_res.status_ret;
}


static int verb_read_serial_number(struct command_transaction *trans)
{
	void *position;
	iap_cmd_res_t iap_cmd_res;

	// Don't allow reads if we can't fit a full response.
	if (trans->data_out_max_length < 16)
		return EINVAL;

	// Read the board's serial number.
	iap_cmd_res.cmd_param.command_code = IAP_CMD_READ_SERIAL_NO;
	iap_cmd_call(&iap_cmd_res);

	// Add in each of the blocks of the serial number.
	position = comms_start_response(trans);
	for (int i = 0; i < 4; ++i)
		comms_response_add_uint32_t(trans, &position, iap_cmd_res.status_res.iap_result[i]);

	return iap_cmd_res.status_res.status_ret;
}

/**
 * Arguments:
 *
 *		value = 0: regular reset
 *		value = 1: switch to an external clock after eset
 */
static int verb_request_reset(struct command_transaction *trans)
{
	uint32_t reset_reason_command = comms_argument_parse_uint32_t(trans);

	if(reset_reason_command == 1) {
		reset_reason = RESET_REASON_USE_EXTCLOCK;
	} else {
		reset_reason = RESET_REASON_SOFT_RESET;
	}

	wwdt_reset(100000);
	return 0;
}

/**
 * Verbs for the core API.
 */
static struct comms_verb core_verbs[] = {
		{ .verb_number = 0x0, .handler = verb_read_board_id },
		{ .verb_number = 0x1, .handler = verb_read_version_string },
		{ .verb_number = 0x2, .handler = verb_read_part_id },
		{ .verb_number = 0x3, .handler = verb_read_serial_number },
		{ .verb_number = 0x4, .handler = verb_request_reset },
		{} // Sentinel
};
COMMS_DEFINE_SIMPLE_CLASS(core_api, CLASS_NUMBER_CORE, "Core API", core_verbs);
