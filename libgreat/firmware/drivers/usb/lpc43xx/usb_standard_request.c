/*
 * This file is part of GreatFET
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <debug.h>

// FIXME: move this to a standard USB library

#include <drivers/usb/lpc43xx/usb.h>
#include <drivers/usb/lpc43xx/usb_type.h>
#include <drivers/usb/lpc43xx/usb_queue.h>

#include <drivers/usb/lpc43xx/usb_standard_request.h>

enum {
	USB_MAX_STRING_LEN = 64,
};

uint8_t usb_string_buffer[(USB_MAX_STRING_LEN * 2) + sizeof(usb_string_descriptor_t)];


/**
 * Handler for the setup stage of a standard request.
 */
typedef usb_request_status_t (*usb_request_handler_t)(usb_endpoint_t *const endpoint);


/**
 * Scheudles a response to a GET_DESCRIPTOR request.
 *
 * @param descriptor The descriptor to send as our reply. NULLs are accepatable, but will
 *		generate a STALL return value.
 * @return A request status; _OK on success or _STALL on failure.
 */
static usb_request_status_t usb_send_descriptor(usb_endpoint_t* const endpoint, usb_descriptor_t *descriptor)
{
	uint32_t length_to_send;

	// If we don't have a valid descriptor, stall.
	if (!descriptor) {
		return USB_REQUEST_STATUS_STALL;
	}

	// Initially, assume we're going to send the full descriptor.
	length_to_send = descriptor->length;

	// If this is a configuration descriptor, this descriptor can contain subordinate
	// descriptors. Accordingly, we'll use its "total length" field
	if (descriptor->type == USB_DESCRIPTOR_TYPE_CONFIGURATION) {
		usb_configuration_descriptor_t *config = (void *)descriptor;
		length_to_send = config->total_length;
	}

	// If the user has requested less than the maximum we have to send, truncate.
	if (endpoint->setup.length < length_to_send) {
		length_to_send = endpoint->setup.length;
	}

	// We cast the const away but this shouldn't be a problem as this is a write transfer
	usb_transfer_schedule_block(endpoint->in, descriptor, length_to_send, NULL, NULL);
	usb_transfer_schedule_ack(endpoint->out);

	return USB_REQUEST_STATUS_OK;
}



/**
 *
 */
static usb_request_status_t usb_send_descriptor_string(usb_endpoint_t* const endpoint)
{
	uint8_t index = endpoint->setup.value_l;

	// Special case: language strings are sent directly.
	if (index == 0) {
		return usb_send_descriptor(endpoint, (usb_descriptor_t *)endpoint->device->language_descriptors);
	}

	// Search each of the string descriptors associated with the device.
	for (uint8_t i = 1; endpoint->device->string_descriptors[i] != 0; i++) {

		// If this is the string descriptor we're looking for, send it.
		if (i == index) {
			usb_descriptor_t *descriptor = endpoint->device->string_descriptors[i];
			return usb_send_descriptor(endpoint, descriptor);
		}
	}

	return USB_REQUEST_STATUS_STALL;
}


/**
 *
 */
static usb_request_status_t usb_standard_request_get_descriptor(usb_endpoint_t* const endpoint)
{
	usb_descriptor_t *descriptor = NULL;

	uint8_t descriptor_type  = endpoint->setup.value_h;
	uint8_t descriptor_index = endpoint->setup.value_l;

	switch (descriptor_type) {
		case USB_DESCRIPTOR_TYPE_DEVICE:
			descriptor = (void *)endpoint->device->device_descriptor;
			break;

		case USB_DESCRIPTOR_TYPE_CONFIGURATION:
			descriptor = (void *)usb_find_configuration_descriptor(endpoint->device, descriptor_index);
			break;

		case USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER:
			descriptor = (void *)endpoint->device->device_qualifier_descriptor;
			break;

		case USB_DESCRIPTOR_TYPE_OTHER_SPEED_CONFIGURATION:
			descriptor = (void *)usb_find_other_speed_configuration_descriptor(endpoint->device, descriptor_index);
			break;

		case USB_DESCRIPTOR_TYPE_STRING:
			return usb_send_descriptor_string(endpoint);

		case USB_DESCRIPTOR_TYPE_INTERFACE:
		case USB_DESCRIPTOR_TYPE_ENDPOINT:
			// FIXME: implement these!

		default:
			return USB_REQUEST_STATUS_STALL;
	}

	return usb_send_descriptor(endpoint, descriptor);
}


static usb_request_status_t usb_standard_request_set_address(usb_endpoint_t* const endpoint)
{
	usb_set_address_deferred(endpoint->device, endpoint->setup.value_l);
	usb_transfer_schedule_ack(endpoint->in);
	return USB_REQUEST_STATUS_OK;
}


static usb_request_status_t usb_standard_request_set_configuration(usb_endpoint_t* const endpoint)
{
	const uint8_t usb_configuration = endpoint->setup.value_l;

	// Attempt to apply the configuration provided.
	int rc = usb_set_configuration(endpoint->device, usb_configuration);

	// If we couldn't apply the relevant configuration, stall.
	if (rc) {
		return USB_REQUEST_STATUS_STALL;
	}

	// The original version of this code autoamtically assigned the device a zero address
	// when it was de-configured.
	usb_transfer_schedule_ack(endpoint->in);
	return USB_REQUEST_STATUS_OK;
}


/**
 *
 */
static usb_request_status_t usb_standard_request_get_configuration(usb_endpoint_t* const endpoint)
{
	static uint8_t configuration_index = 0;

	// If this isn't exactly the one byte needed to communicate
	// a configuration index, fail out.
	if (endpoint->setup.length != sizeof(configuration_index) ) {
		return USB_REQUEST_STATUS_STALL;
	}

	// If the device is currently configured, transmit its number.
	if (endpoint->device->active_configuration) {
		configuration_index = endpoint->device->active_configuration->value;
	}

	usb_transfer_schedule_block(endpoint->in, &configuration_index, sizeof(configuration_index), NULL, NULL);
	usb_transfer_schedule_ack(endpoint->out);

	return USB_REQUEST_STATUS_OK;
}


/**
 *
 */
static usb_request_status_t usb_standard_request_get_status(usb_endpoint_t* const endpoint)
{
	static uint16_t status = 0;

	// If this isn't exactly the one byte needed to communicate
	// a configuration index, fail out.
	if (endpoint->setup.length != sizeof(status)) {
		return USB_REQUEST_STATUS_STALL;
	}

	usb_transfer_schedule_block(endpoint->in, &status, sizeof(status), NULL, NULL);
	usb_transfer_schedule_ack(endpoint->out);

	return USB_REQUEST_STATUS_OK;
}


/**
 * Default handlers for USB requests we don't handle; always stalls.
 */
static usb_request_status_t usb_standard_request_unhandled(usb_endpoint_t* const endpoint)
{
	(void)endpoint;
	return USB_REQUEST_STATUS_STALL;
}


/**
 *
 */
static usb_request_handler_t usb_get_handler_for_standard_request(uint8_t request)
{
	// If we support the given request, return an appropriate handler.
	switch (request) {
		case USB_STANDARD_REQUEST_GET_STATUS:
			return usb_standard_request_get_status;
		case USB_STANDARD_REQUEST_GET_DESCRIPTOR:
			return usb_standard_request_get_descriptor;
		case USB_STANDARD_REQUEST_SET_ADDRESS:
			return usb_standard_request_set_address;
		case USB_STANDARD_REQUEST_SET_CONFIGURATION:
			return usb_standard_request_set_configuration;
		case USB_STANDARD_REQUEST_GET_CONFIGURATION:
			return usb_standard_request_get_configuration;
	}

	return usb_standard_request_unhandled;
}

/**
 */
usb_request_status_t usb_standard_request(usb_endpoint_t* const endpoint,
	const usb_transfer_stage_t stage)
{
	uint8_t request = endpoint->setup.request;

	// Try to find the handler for the relevant request.
	usb_request_handler_t handler =
		usb_get_handler_for_standard_request(request);

	// If we couldn't find a handler, stall.
	if (!handler) {
		return USB_REQUEST_STATUS_STALL;
	}

	// If this is the setup stage, execute the core standard handler.
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		return handler(endpoint);
	}

	// Assuming we have a handler, ACK the relevant request.
	return USB_REQUEST_STATUS_OK;
}
