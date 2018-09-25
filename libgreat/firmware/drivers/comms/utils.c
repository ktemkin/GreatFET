/*
 * This file is part of libgreat
 *
 * High-level communications API -- convenience functions.
 */

#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <drivers/comms.h>

/**
 * Dark metaprogramming magic.
 * If only C had the Rust-like macros. :)
 */
#define COMMS_DEFINE_RESPONSE_HANDLER(type) \
	void *comms_response_add_##type(struct command_transaction *trans, void **data_out, type response) \
	{ \
		type *target = *data_out; \
		\
		if (trans->data_out_max_length < sizeof(type)) \
			return *data_out; \
		\
		trans->data_out_length += sizeof(type); \
		*target = response; \
		\
		++target; \
		*data_out = target; \
		return target; \
	} \
	\
	void *comms_respond_##type(struct command_transaction *trans, type response) \
	{ \
		void *position = comms_start_response(trans); \
		return comms_response_add_##type(trans, &position, response); \
	} 

#define COMMS_DEFINE_ARGUMENT_HANDLER(type) \
	type comms_argument_parse_##type(struct command_transaction *trans) \
	{ \
		type *target = trans->data_in; \
		return *target; \
	}

/** Quick response handling functions. */
COMMS_DEFINE_RESPONSE_HANDLER(uint8_t);
COMMS_DEFINE_RESPONSE_HANDLER(uint16_t);
COMMS_DEFINE_RESPONSE_HANDLER(uint32_t);
COMMS_DEFINE_RESPONSE_HANDLER(int8_t);
COMMS_DEFINE_RESPONSE_HANDLER(int16_t);
COMMS_DEFINE_RESPONSE_HANDLER(int32_t);

/** Quick argument read functions. */
COMMS_DEFINE_ARGUMENT_HANDLER(uint8_t);
COMMS_DEFINE_ARGUMENT_HANDLER(uint16_t);
COMMS_DEFINE_ARGUMENT_HANDLER(uint32_t);
COMMS_DEFINE_ARGUMENT_HANDLER(int8_t);
COMMS_DEFINE_ARGUMENT_HANDLER(int16_t);
COMMS_DEFINE_ARGUMENT_HANDLER(int32_t);

void *comms_respond_string(struct command_transaction *trans, char *response)
{
	// Copy the string into our response buffer.
	size_t length = strlen(response);
	strlcpy(trans->data_out, response, trans->data_out_max_length);

	// Truncate the length to the maximum response.
	if (length > trans->data_out_max_length)
		length = trans->data_out_max_length;

	// Store the actual length transmitted, and advance our pointer.
	trans->data_out_length = length;
	response += length;

	return response;
}

/**
 * Convenience function that starts an (empty) response,
 * for later use with the comms_response_add_N functions.
 *
 * @param trans The transaction to respond to.
 * @return A pointer to be passed as the second argument into future add_ functions.
 */
void *comms_start_response(struct command_transaction *trans)
{
	trans->data_out_length = 0;
	return trans->data_out;
}
