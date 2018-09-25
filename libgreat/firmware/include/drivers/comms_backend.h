/*
 * This file is part of libgreat
 *
 * High-level communications API -- used by all devices that wish to speak
 * the standard communications protocol.
 */

#include <drivers/usb/lpc43xx/usb_type.h>
#include <drivers/usb/lpc43xx/usb_request.h>
#include <toolchain.h>

/**
 * Core communications driver for libgreat.
 */
struct comm_backend_driver {

    /** The name of the driver, for e.g. logging. */
    char *name;

};

/**
 * Structure representing a command header for a libgreat command.
 */
struct ATTR_PACKED libgreat_command_prelude {
	uint32_t class_number;
	uint32_t verb;
};


/**
 * Submits a command for execution. Used by command backends.
 *
 * @param backend The command backend driver submitting the given command.
 * @param trans An object representing the command to be submitted, and its
 *		response.
 */
int comms_backend_submit_command(struct comm_backend_driver *backend, 
	struct command_transaction *trans);


