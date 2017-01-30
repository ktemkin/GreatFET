/*
 * Copyright 2017 Kyle J. Temkin <kyle@ktemkin.com>
 *
 * This file is part of GreatFET.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "usb_api_spi.h"
#include "usb_queue.h"

#include <stddef.h>
#include "usb_api_greatdancer.h"

#include <libopencm3/cm3/vector.h>
#include <libopencm3/lpc43xx/m4/nvic.h>

#include <greatfet_core.h>

#include "usb.h"
#include "usb_standard_request.h"
#include "usb_descriptor.h"
#include "usb_device.h"
#include "usb_endpoint.h"
#include "usb_request.h"

struct _endpoint_setup_command_t {
  uint8_t address;
  uint16_t max_packet_size;
  uint8_t transfer_type;
} __attribute__((packed));
typedef struct _endpoint_setup_command_t endpoint_setup_command_t;

typedef char packet_buffer[1024];

packet_buffer transfer_buffer; // TODO: dissolve me into the endpoint buffers below?

packet_buffer endpoint_buffer[NUM_USB1_ENDPOINTS];
uint32_t total_received_data[NUM_USB1_ENDPOINTS];

/**
 * When using the GreatDancer, all events are generated and handled on the host side.
 */
const usb_request_handlers_t usb1_request_handlers = {
	.standard = NULL,
	.class = NULL,
	.vendor = NULL,
	.reserved = NULL,
};

void usb1_configuration_changed(usb_device_t* const device)
{
	if( device->configuration->number == 1 ) {
		led_on(LED1);
	}
}

void init_greatdancer_api(void) {
  // Initialize all of our queues, so they're ready
  // if the GreatDancer application decides to use them.
	usb_queue_init(&usb1_endpoint_control_out_queue);
	usb_queue_init(&usb1_endpoint_control_in_queue);
	usb_queue_init(&usb1_endpoint1_out_queue);
	usb_queue_init(&usb1_endpoint1_in_queue);
	usb_queue_init(&usb1_endpoint2_out_queue);
	usb_queue_init(&usb1_endpoint2_in_queue);
	usb_queue_init(&usb1_endpoint3_out_queue);
	usb_queue_init(&usb1_endpoint3_in_queue);
}

static void set_up_greatdancer(void) {
	usb_set_configuration_changed_cb(usb1_configuration_changed);

	usb_peripheral_reset(&usb1_device);

	// XXX UNDO CHANGE
	usb_device_init(&usb1_device, true);

  // Set up the control endpoint. The application will request setup
  // for all of the non-standard channels on connection.
	usb_endpoint_init(&usb1_endpoint_control_out);
	usb_endpoint_init(&usb1_endpoint_control_in);
}

static usb_endpoint_t *usb_preinit_endpoint_from_address(uint8_t address)
{
    switch(address) {
        case 0x80: return &usb1_endpoint_control_in;
        case 0x00: return &usb1_endpoint_control_out;

        case 0x81: return &usb1_endpoint1_in;
        case 0x01: return &usb1_endpoint1_out;

        case 0x82: return &usb1_endpoint2_in;
        case 0x02: return &usb1_endpoint2_out;

        case 0x83: return &usb1_endpoint3_in;
        case 0x03: return &usb1_endpoint3_out;
    }

    return NULL;
}

/**
 * Sets up the GreatDancer to make a USB connection.
 *
 * Expects zero or more triplets describing how the device's endpoints should
 * be initialied. Each triplet should contain:
 *
 * - One byte of endpoint address
 * - Two bytes describing the maximum packet size on the endpoint
 * - One byte describing the endpoint type
 */
usb_request_status_t usb_vendor_request_greatdancer_connect(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {

    usb_controller_reset(&usb1_device);
    set_up_greatdancer();

    // Note that we call usb_controller_run and /not/ usb_run.
    // This in particular leaves all interrupts masked in the NVIC
    // so we can poll them manually.
    usb_controller_run(&usb1_device);

		usb_transfer_schedule_ack(endpoint->in);
	}
	return USB_REQUEST_STATUS_OK;
}


/**
 * Sets up the GreatDancer to make a USB connection.
 *
 * Expects zero or more triplets describing how the device's endpoints should
 * be initialied. Each triplet should contain:
 *
 * - One byte of endpoint address
 * - Two bytes describing the maximum packet size on the endpoint
 * - One byte describing the endpoint type
 */
usb_request_status_t usb_vendor_request_greatdancer_set_up_endpoints(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {

		// Read the data to be transmitted from the host.
		usb_transfer_schedule_block(endpoint->out, transfer_buffer, endpoint->setup.length, NULL, NULL);

  } else if(stage == USB_TRANSFER_STAGE_DATA) {
    int i;
    endpoint_setup_command_t *command = NULL;

    // Set up any endpoints we'll be using.
    for(i = 0; i < endpoint->setup.length; i += 4) {
        usb_endpoint_t* target_endpoint;
        command = (endpoint_setup_command_t *)&transfer_buffer[i];

        // Set up the given endpoint.
        target_endpoint = usb_preinit_endpoint_from_address(command->address);
        usb_endpoint_init_without_descriptor(target_endpoint, command->max_packet_size, command->transfer_type);
    }

		usb_transfer_schedule_ack(endpoint->in);
	}
	return USB_REQUEST_STATUS_OK;
}




/**
 * Terminates all existing communication and shuts down the GreatDancer USB.
 */
usb_request_status_t usb_vendor_request_greatdancer_disconnect(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {
    // shut down here
    //
    usb_controller_reset(&usb1_device);

		usb_transfer_schedule_ack(endpoint->in);
	}
	return USB_REQUEST_STATUS_OK;
}


/**
 * Queries the GreatDancer for any events that need to be processed.
 */
usb_request_status_t usb_vendor_request_greatdancer_get_status(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		// Get the device status, and send it back to the host.
		const uint32_t status = usb_get_status(&usb1_device);
		usb_transfer_schedule_block(endpoint->in, (void * const)&status, sizeof(status), NULL, NULL);
	} else if (stage == USB_TRANSFER_STAGE_DATA) {
		usb_transfer_schedule_ack(endpoint->out);
	}
	return USB_REQUEST_STATUS_OK;
}

// FIXME: deuplicate code with other status queries
usb_request_status_t usb_vendor_request_greatdancer_get_setup_status(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		// Get the device status, and send it back to the host.
		const uint32_t endptsetupstat = usb_get_endpoint_setup_status(&usb1_device);
		usb_transfer_schedule_block(endpoint->in, (void * const)&endptsetupstat, sizeof(endptsetupstat), NULL, NULL);
	} else if (stage == USB_TRANSFER_STAGE_DATA) {
		usb_transfer_schedule_ack(endpoint->out);
	}
	return USB_REQUEST_STATUS_OK;
}

// FIXME: deuplicate code with other status queries
usb_request_status_t usb_vendor_request_greatdancer_get_transfer_status(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		// Get the device status, and send it back to the host.
		const uint32_t endptcomplete = usb_get_endpoint_complete(&usb1_device);
		usb_transfer_schedule_block(endpoint->in, (void * const)&endptcomplete, sizeof(endptcomplete), NULL, NULL);
	} else if (stage == USB_TRANSFER_STAGE_DATA) {
		usb_transfer_schedule_ack(endpoint->out);
	}
	return USB_REQUEST_STATUS_OK;
}

// FIXME: deuplicate code with other status queries
usb_request_status_t usb_vendor_request_greatdancer_get_transfer_readiness(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		// Get the device status, and send it back to the host.
		const uint32_t endptstatus = usb_get_endpoint_ready(&usb1_device);
		usb_transfer_schedule_block(endpoint->in, (void * const)&endptstatus, sizeof(endptstatus), NULL, NULL);
	} else if (stage == USB_TRANSFER_STAGE_DATA) {
		usb_transfer_schedule_ack(endpoint->out);
	}
	return USB_REQUEST_STATUS_OK;
}




/**
 * Reads a setup packet from the GreatDancer port and relays it to the host.
 * The index parameter specifies which endpoint we should be reading from.
 */
usb_request_status_t usb_vendor_request_greatdancer_read_setup(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		int endpoint_number = endpoint->setup.index;

		// Figure out the endpoint we're reading setup data from...
		uint_fast8_t address = usb_endpoint_address(USB_TRANSFER_DIRECTION_OUT, endpoint_number);
		usb_endpoint_t* const target_endpoint = usb_endpoint_from_address(address, &usb1_device);

		// ... and find its setup data.
		uint8_t * const setup_data =
			(uint8_t * const)usb_queue_head(target_endpoint->address, target_endpoint->device)->setup;

		// Transmit the setup data back ...
		usb_transfer_schedule_block(endpoint->in, setup_data, 8, NULL, NULL);
		
		// ... and mark that packet as handled.
		usb_clear_endpoint_setup_status(1 << endpoint_number, &usb1_device);

	} else if (stage == USB_TRANSFER_STAGE_DATA) {
		usb_transfer_schedule_ack(endpoint->out);
	}
	return USB_REQUEST_STATUS_OK;
}

usb_request_status_t usb_vendor_request_greatdancer_ack_status(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		int endpoint_number = endpoint->setup.index;

		// Figure out the endpoint we're reading setup data from...
		uint_fast8_t address = usb_endpoint_address(USB_TRANSFER_DIRECTION_IN, endpoint_number);
		usb_endpoint_t* const target_endpoint = usb_endpoint_from_address(address, &usb1_device);

		// Send an acknolwedgement on the relevant endpoint.
    usb_transfer_schedule_ack(target_endpoint);

		// Send the acknolwedgement for the control channel...
		usb_transfer_schedule_ack(endpoint->in);
	}
	return USB_REQUEST_STATUS_OK;
}

static void store_transfer_count_callback(void * const user_data, unsigned int transferred)
{
    unsigned int * total_data = (unsigned int*)user_data;
    *total_data = transferred;
}

/**
 * Primes the USB controller to recieve data on a particular endpoint, but
 * does not wait for a transfer to complete. The transfer's status can be
 * checked with get_transfer_status and then read with finish_nonblocking_read.
 *
 * The index parameter specifies which endpoint we should be reading from.
 */
usb_request_status_t usb_vendor_request_greatdancer_start_nonblocking_read(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		int endpoint_number = endpoint->setup.index;

		// Figure out the endpoint we're reading setup data from...
		uint_fast8_t address = usb_endpoint_address(USB_TRANSFER_DIRECTION_OUT, endpoint_number);
		usb_endpoint_t* const target_endpoint = usb_endpoint_from_address(address, &usb1_device);

		// ... and start a nonblocking transfer.
		usb_transfer_schedule(target_endpoint, &endpoint_buffer[endpoint_number], sizeof(packet_buffer), store_transfer_count_callback, &total_received_data[endpoint_number]);
		usb_transfer_schedule_ack(endpoint->in);
	}
	return USB_REQUEST_STATUS_OK;
}


// FIXME: deuplicate code with other status queries
usb_request_status_t usb_vendor_request_greatdancer_get_nonblocking_data_length(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		// Get the total data read, and send it back to the host.
		int endpoint_number = endpoint->setup.index;
		usb_transfer_schedule_block(endpoint->in, (void * const)&total_received_data[endpoint_number], sizeof(total_received_data[endpoint_number]), NULL, NULL);
	} else if (stage == USB_TRANSFER_STAGE_DATA) {
		usb_transfer_schedule_ack(endpoint->out);
	}
	return USB_REQUEST_STATUS_OK;
}




/**
 * Finishes a non-blocking read by returning the read data back to the host.
 * Should only be used after determining that a transfer is complete with 
 * the get_transfer_status request.
 *
 * index: The endpoint number to request data on.
 */
usb_request_status_t usb_vendor_request_greatdancer_finish_nonblocking_read(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		int endpoint_number = endpoint->setup.index;

    // XXX FIXME XXX
    // check length <= buffer size

    // Transmit the data back.
		usb_transfer_schedule_block(endpoint->in, &endpoint_buffer[endpoint_number], endpoint->setup.length, NULL, NULL);

	} else if (stage == USB_TRANSFER_STAGE_DATA) {
		usb_transfer_schedule_ack(endpoint->out);
	}
	return USB_REQUEST_STATUS_OK;
}


/**
 * Reads data from a GreatDancer endpoint and relays it to the host.
 * The index parameter specifies which endpoint we should be reading from.
 */
usb_request_status_t usb_vendor_request_greatdancer_read_from_endpoint(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		int endpoint_number = endpoint->setup.index;

		// Figure out the endpoint we're reading setup data from...
		uint_fast8_t address = usb_endpoint_address(USB_TRANSFER_DIRECTION_OUT, endpoint_number);
		usb_endpoint_t* const target_endpoint = usb_endpoint_from_address(address, &usb1_device);

		// Read the requested amount of data from the endpoint.
		usb_transfer_schedule_block(target_endpoint, transfer_buffer, sizeof(transfer_buffer), NULL, NULL);

		// Transmit the setup data back ...
		usb_transfer_schedule_block(endpoint->in, transfer_buffer, sizeof(transfer_buffer), NULL, NULL);


	} else if (stage == USB_TRANSFER_STAGE_DATA) {
		usb_transfer_schedule_ack(endpoint->out);
	}
	return USB_REQUEST_STATUS_OK;
}

/**
 * Reads data from the GreatFET host and sends on a provided GreatDancer endpoint.
 * The index parameter specifies which endpoint we should be reading from.
 *
 * FIXME: support-multi-buffer transfers
 */
usb_request_status_t usb_vendor_request_greatdancer_send_on_endpoint(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
  int endpoint_number = endpoint->setup.index;

  // Figure out the endpoint we're reading setup data from...
  uint_fast8_t address = usb_endpoint_address(USB_TRANSFER_DIRECTION_IN, endpoint_number);
  usb_endpoint_t* const target_endpoint = usb_endpoint_from_address(address, &usb1_device);

	if (stage == USB_TRANSFER_STAGE_SETUP) {

    // FIXME XXX THIS WILL BREAK IF setup.length > sizeof(transfer_buffer)
    // (and this is technically an infoleak, but it's a PoC)
    
    // If we have a ZLP, handle it immediately.
    if(endpoint->setup.length == 0 ) {
    
      usb_transfer_schedule_ack(target_endpoint);
      usb_transfer_schedule_ack(endpoint->in);

    } else {

      // Read the data to be transmitted from the host.
      usb_transfer_schedule_block(endpoint->out, transfer_buffer, endpoint->setup.length, NULL, NULL);

    }

	} else if (stage == USB_TRANSFER_STAGE_DATA) {

      if(endpoint->setup.length > 0 ) {

        // Send the data on the endpoint.
        usb_transfer_schedule(target_endpoint, transfer_buffer, endpoint->setup.length, NULL, NULL);

        usb_transfer_schedule_ack(endpoint->in);
    }
	}
	return USB_REQUEST_STATUS_OK;
}




/**
 * Reads data from a GreatDancer endpoint and relays it to the host.
 * The index parameter specifies which endpoint we should be reading from.
 */
usb_request_status_t usb_vendor_request_greatdancer_set_address(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		uint_fast8_t address = endpoint->setup.value_l;
		usb_set_address_immediate(&usb1_device, address);
		usb_transfer_schedule_ack(endpoint->in);
	}
	return USB_REQUEST_STATUS_OK;
}


usb_request_status_t usb_vendor_request_greatdancer_bus_reset(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		usb_bus_reset(&usb1_device);
		usb_transfer_schedule_ack(endpoint->in);
	}
	return USB_REQUEST_STATUS_OK;
}


usb_request_status_t usb_vendor_request_greatdancer_stall_endpoint(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		int endpoint_number = endpoint->setup.index;

		// Figure out the endpoint we're reading setup data from...
		uint_fast8_t address = usb_endpoint_address(USB_TRANSFER_DIRECTION_OUT, endpoint_number);
		usb_endpoint_t* const target_endpoint = usb_endpoint_from_address(address, &usb1_device);

		usb_endpoint_stall(target_endpoint);
		usb_transfer_schedule_ack(endpoint->in);
	}
	return USB_REQUEST_STATUS_OK;
}





/**
 * Should be called whenever a transfer is complete; cleans up any transfer
 * descriptors assocaited with that transfer.
 *
 * index: The endpoint on which the transfer should be cleaned up.
 * value: The direction; matches the USB spec. (1 for IN)
 */
usb_request_status_t usb_vendor_request_greatdancer_clean_up_transfer(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		int endpoint_number = endpoint->setup.index;
		int direction = (endpoint->setup.value_l) ? USB_TRANSFER_DIRECTION_IN : USB_TRANSFER_DIRECTION_OUT;

		// Figure out the endpoint we're reading setup data from...
		uint_fast8_t address = usb_endpoint_address(direction, endpoint_number);
		usb_endpoint_t* const target_endpoint = usb_endpoint_from_address(address, &usb1_device);

		// Clear the "transfer complete" bit.
		if(direction == USB_TRANSFER_DIRECTION_IN) {
			usb_clear_endpoint_complete(USB1_ENDPTCOMPLETE_ETCE(1 << endpoint_number), &usb1_device);
		} else {
			usb_clear_endpoint_complete(USB1_ENDPTCOMPLETE_ERCE(1 << endpoint_number), &usb1_device);
		}

		// Clean up any transfers that are complete on the given endpoint.
		usb_queue_transfer_complete(target_endpoint);

		// Send the acknolwedgement for the control channel...
		usb_transfer_schedule_ack(endpoint->in);
	}
	return USB_REQUEST_STATUS_OK;
}
