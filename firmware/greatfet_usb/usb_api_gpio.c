/*
 * Copyright 2016 Kyle J. Temkin <kyle@ktemkin.com>
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
 *
 */

/**
 * USB API for generic (slow) GPIO control.
 */

#include "usb_api_spiflash.h"
#include "usb_queue.h"

#include <stddef.h>
#include <greatfet_core.h>
#include <gpio_lpc.h>

/* TODO: Consolidate ARRAY_SIZE declarations */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Type used by funcitons that handle GPIO register writes. */
typedef void (*gpio_write_operation)(uint8_t port_number,
		uint32_t mask, uint32_t value);

/* Buffer for GPIO usb transfers. */
static uint32_t data_arguments[2];

/**
 * Handles a write request to the GPIO data direction register.
 *
 * port_number: The GPIO port number; should be between 0-7.
 * mask: A 32-bit mask describing which bits should be affected.
 * value: The value to be applied. Bits not in the given mask are ignored.
 */
static void handle_data_direction_write(uint8_t port_number, uint32_t mask, uint32_t value)
{
		volatile gpio_port_t *port = GPIO_LPC_PORT(port_number);
		uint32_t data_direction, masked_value;

		/* Clear any bits set in our desired value, so we can use it as an OR-mask. */
		masked_value = value & ~mask;

		/* And mask in any relevant bits to our data direction register. */
		data_direction = port->dir;
		data_direction &= ~mask;
		data_direction |= masked_value;

		/* Finally, set the data direction register to its new value. */
		port->dir = data_direction;
}

/**
 * Handles a write request to the GPIO data direction register.
 *
 * port_number: The GPIO port number; should be between 0-7.
 * mask: A 32-bit mask describing which bits should be affected.
 * value: The value to be applied. Bits not in the given mask are ignored.
 */
static void handle_gpio_port_write(uint8_t port_number, uint32_t mask, uint32_t value)
{
		volatile gpio_port_t *port = GPIO_LPC_PORT(port_number);
		uint32_t orig_mask;

		/* Set the port mask to match our mask, set our new value, and restore the
		 * original mask. This effectively uses the LPC's hardware to to realize
		 * our masking functionality. Note that LPC's mask is in the inverse of ours. */
		orig_mask = port->mask;
		port->mask = ~mask;

		port->mpin = value;

		port->mask = orig_mask;
}

/* Handlers for GPIO write requests. */
static const gpio_write_operation gpio_write_handlers[] = {
	handle_data_direction_write, // 0
	handle_gpio_port_write       // 1
};


usb_request_status_t usb_vendor_request_gpio_write(
	usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	/* Ensure that we've been sent valid arguments. */
	if(endpoint->setup.length != sizeof(data_arguments))
			return USB_REQUEST_STATUS_STALL;

	/* Set the stage for our GPIO operation: read in our arguments. */
	if (stage == USB_TRANSFER_STAGE_SETUP) {
			usb_transfer_schedule_block(endpoint->out, data_arguments, sizeof(data_arguments), NULL, NULL);
			return USB_REQUEST_STATUS_OK;
	} 
	/* Next, handle the GPIO operation itself. */
	else if (stage == USB_TRANSFER_STAGE_DATA) {
			uint8_t port_number = endpoint->setup.index;
			uint16_t operation_number = endpoint->setup.value;
			uint32_t mask = data_arguments[0];
			uint32_t new_value = data_arguments[1];
			gpio_write_operation operation;

			/* If we don't have a realizable port number, abort. */
			if(port_number > 7)
					return USB_REQUEST_STATUS_STALL;

			/* If this is an invalid operation, abort. */
			if(operation_number >= ARRAY_SIZE(gpio_write_handlers))
					return USB_REQUEST_STATUS_STALL;

			/* Otherwise, call the appropriate operation. */
			operation = gpio_write_handlers[operation_number];
			operation(port_number, mask, new_value);

			/* ACK.*/
			usb_transfer_schedule_ack(endpoint->in);
			return USB_REQUEST_STATUS_OK;
	}
	/* Ignore any other stage. */
	else {
		return USB_REQUEST_STATUS_OK;
	}
}

