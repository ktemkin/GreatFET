/*
 * Copyright (c) Kyle J. Temkin <kyle@ktemkin.com>
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

#ifndef __USB_API_GPIO_H__
#define __USB_API_GPIO_H__

#include <usb_type.h>
#include <usb_request.h>

enum gpio_register_type {
	gpio_data_direction = 0,  // Specifies the data direction of the given port.
	gpio_port_pins = 1,       // Specifies the actual pin values for the given port.
	// TODO: set/clear/toggle ?
};

/**
 * OUT Vendor request that performs 'low-level' modification of the LPC4330's
 * GPIO control registers. Allows simple, low-level access to the GPIO to be 
 * exposed to the remote API.
 *
 * Request components:
 *		index: The number of the port to be affected. Should be 0-7.
 *		value: The type of GPIO register to be adjusted, from the gpio_register_type
 *				enumeration. See its documentation above.
 *		data: Two 32-bit words:
 *				Word 0 (first four bytes): A mask that will be used to determine which
 *				bits of the given word are affected.
 */
usb_request_status_t usb_vendor_request_gpio_write(
	usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage);


/**
 * IN Vendor request that performs 'low-level' reads of the LPC4330's
 * GPIO control registers.
 *
 * Request components:
 *		index: The number of the port to be read. Should be 0-7.
 *		value: The type of GPIO register to be read, from the gpio_register_type
 *				enumeration. See its documentation above.
 *
 *		Provides a single 32-bit word as resultant data.
 */
usb_request_status_t usb_vendor_request_gpio_read(
	usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage);

#endif /* end of include guard: __USB_API_GPIO_H__ */
