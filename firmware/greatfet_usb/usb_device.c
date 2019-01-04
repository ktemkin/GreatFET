/*
 * This file is part of GreatFET
 */

#include "usb_device.h"
#include <drivers/usb/lpc43xx/usb_type.h>

#include <stdint.h>
#include <stddef.h>
#include <rom_iap.h>
#include <string.h>


/**
 * Currently, the GreatFET has a configuration descriptor with three
 * subordinates: a single interface that owns two subordinate endpoints.
 */
typedef struct greatfet_composite_configuration {
	usb_configuration_descriptor_t configuration;
	usb_interface_descriptor_t interface;
	usb_endpoint_descriptor_t endpoints[2];
} ATTR_PACKED greatfet_composite_configuration_t;


/**
 * Buffer that will store the to-be-generated ASCII string
 * that stores an ASCII representation of our serial number.
 */
uint8_t serial_number_string[(USB_DESCRIPTOR_STRING_SERIAL_LEN * 2) + 2];

/**
 * The device descriptor for the GreatFET.
 */
static usb_device_descriptor_t device_descriptor = {
	.length                     = sizeof(usb_device_descriptor_t),
	.type                       = USB_DESCRIPTOR_TYPE_DEVICE,

	// Our controller is USB2.0 compliant.
	.usb_version                = {.high_digit = 2, .low_digit = 0},

	// We're a composite device.
	.device_class               = 0x00,
	.device_subclass            = 0x00,
	.device_protocol            = 0x00,

	// We'll default to an EP0 max packet size of 64,
	// which works on both full and high speed devices.
	.ep0_max_packet_size        = 64,

	// Our USB VID/PID.
	.vendor_id                  = 0x1d50,
	.product_id                 = 0x60e6,

	// Start off with a device hardware version of 1.0.
	.device_version             = {.high_digit = 1, .low_digit = 0},

	// String indices for the device's general descriptions.
	.manufacturer_string_index  = 1,
	.product_string_index       = 2,
	.serial_string_index        = 3,

	// We'll only present a single configuration, for now.
	.configuration_count        = 1
};

/**
 * Our device qualifier descriptor summzarizes our differences
 * between our low and full speed.
 */
static usb_device_qualifier_descriptor_t device_qualifier_descriptor = {
	.length                     = sizeof(usb_device_qualifier_descriptor_t),
	.type                       = USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,

	// We're a composite device.
	.device_class               = 0x00,
	.device_subclass            = 0x00,
	.device_protocol            = 0x00,

	// We'll default to an EP0 max packet size of 64,
	// which works on both full and high speed devices.
	.ep0_max_packet_size        = 64,

	// TODO: Figure out why this value is 2 instead of 1?
	.configuration_count        = 2,
};


/**
 * The core configuration descriptor; which is the first and core part
 * of our configuration descriptor.
 */
static usb_configuration_descriptor_t configuration_descriptor = {
	.length                     = sizeof(usb_configuration_descriptor_t),
	.type                       = USB_DESCRIPTOR_TYPE_CONFIGURATION,

	// Our total lenght includes our subordinate descriptors; so it's the length
	// of our composite configuration data structure.
	.total_length               = sizeof(greatfet_composite_configuration_t),

	// For now, we'll only expose a single custom interface.
	.interface_count            = 1,

	// This is our first configuration.
	.value                      = 1,

	// For now, we'll not label this configuration string,
	// as it's the only configuration we support.
	.string_index               = 0,

	// For now, we don't support waking up the USB bus ourself.
	// Once we're suspended, we'll wait for the host to wake us up.
	.remote_wakeup              = 0,

	// Report being bus-powered for now; though we'll need to adjust this
	// based on the presence of e.g. Jasmine, the battery neighbor.
	.self_powered               = 0,

	// Per the spec, we need to set this bit to '1', always.
	// It's also accurate. ^_^
	.bus_powered                = 1,

	// For now, always request the maximum power. We may want to adjust this
	// in the future
	.current_consumption        = CURRENT_DRAW_IN_MILLIAMPS(500)
};


/**
 * The core interface descriptor, which describes the main GreatFET
 * bulk communications interface.
 */
static usb_interface_descriptor_t interface_descriptor = {
	.length                     = sizeof(usb_interface_descriptor_t),
	.type                       = USB_DESCRIPTOR_TYPE_INTERFACE,

	// This is our first (zero-indexed) index.
	.number                     = 0,

	// For now, we support two bulk endpoints; one in each direction.
	.endpoint_count             = 2,

	// This interface speaks all vendor-specific protocols.
	.device_class               = 0xFF,
	.device_subclass            = 0xFF,
	.device_protocol            = 0xFF,

	// For now, don't annotate the interface with a string.
	// Later, when we have more than one interface, we may want
	// to explicitly label this a GreatFET
	.string_index               = 0,
};

/**
 * We'll need four endpoint descriptors:
 *		- One for the bulk IN endpoint on a high speed device;
 *		- One for the bulk OUT on a high speed device;
 *		- One for the bulk IN on a full speed device; and
 *		- One for the bulk OUT on a full speed device.
 */
static greatfet_composite_configuration_t composite_config_descriptor_hs = {
	.endpoints = {
		{
			.length                     = sizeof(usb_endpoint_descriptor_t),
			.type                       = USB_DESCRIPTOR_TYPE_ENDPOINT,

			// This is EP1 OUT.
			.number                     = 1,
			.direction                  = USB_TRANSFER_DIRECTION_IN,

			// This is a bulk endpoint: it'll be used for mass exchange of data.
			.transfer_type              = USB_TRANSFER_TYPE_BULK,

			// The maximum packet size should matched to our speed.
			. max_packet_size           = USB_MAXIMUM_PACKET_SIZE_HIGH_SPEED,
		},
		{
			.length                     = sizeof(usb_endpoint_descriptor_t),
			.type                       = USB_DESCRIPTOR_TYPE_ENDPOINT,

			// This is EP1 OUT.
			.number                     = 1,
			.direction                  = USB_TRANSFER_DIRECTION_OUT,

			// This is a bulk endpoint: it'll be used for mass exchange of data.
			.transfer_type              = USB_TRANSFER_TYPE_BULK,

			// The maximum packet size should matched to our speed.
			. max_packet_size           = USB_MAXIMUM_PACKET_SIZE_HIGH_SPEED,
		}
	}
};

static greatfet_composite_configuration_t composite_config_descriptor_fs = {
	.endpoints     = {
		{
			.length                     = sizeof(usb_endpoint_descriptor_t),
			.type                       = USB_DESCRIPTOR_TYPE_ENDPOINT,

			// This is EP1 OUT.
			.number                     = 1,
			.direction                  = USB_TRANSFER_DIRECTION_IN,

			// This is a bulk endpoint: it'll be used for mass exchange of data.
			.transfer_type              = USB_TRANSFER_TYPE_BULK,

			// The maximum packet size should matched to our speed.
			. max_packet_size           = USB_MAXIMUM_PACKET_SIZE_FULL_SPEED,
		},
		{
			.length                     = sizeof(usb_endpoint_descriptor_t),
			.type                       = USB_DESCRIPTOR_TYPE_ENDPOINT,

			// This is EP1 OUT.
			.number                     = 1,
			.direction                  = USB_TRANSFER_DIRECTION_OUT,

			// This is a bulk endpoint: it'll be used for mass exchange of data.
			.transfer_type              = USB_TRANSFER_TYPE_BULK,

			// The maximum packet size should matched to our speed.
			. max_packet_size           = USB_MAXIMUM_PACKET_SIZE_FULL_SPEED,
		}
	}
};

/**
 * FIXME: simplify me now that we can?
 */
void usb_set_descriptor_by_serial_number(void)
{
	int position = 0;
	iap_cmd_res_t iap_cmd_res;

	/* Read IAP Serial Number Identification */
	iap_cmd_res.cmd_param.command_code = IAP_CMD_READ_SERIAL_NO;
	iap_cmd_call(&iap_cmd_res);

	if (iap_cmd_res.status_res.status_ret != CMD_SUCCESS) {
		return;
	}

	serial_number_string[position++] = USB_DESCRIPTOR_STRING_SERIAL_LEN;
	serial_number_string[position++] = USB_DESCRIPTOR_TYPE_STRING;

	/* 32 characters of serial number, convert to UTF16-LE */
	for (size_t i=0; i < USB_DESCRIPTOR_STRING_SERIAL_LEN; i++) {
		const uint8_t nibble = (iap_cmd_res.status_res.iap_result[i >> 3] >> (28 - (i & 7) * 4)) & 0xf;
		const char16_t c = (nibble > 9) ? ('a' + nibble - 10) : ('0' + nibble);

		serial_number_string[position++] = c;
		serial_number_string[position++] = 0;
	}
}


static void set_up_descriptors(void)
{
	// Configuration
	memcpy(&composite_config_descriptor_hs.configuration, &configuration_descriptor, sizeof(configuration_descriptor));
	memcpy(&composite_config_descriptor_fs.configuration, &configuration_descriptor, sizeof(configuration_descriptor));

	// Interface
	memcpy(&composite_config_descriptor_hs.interface, &interface_descriptor, sizeof(interface_descriptor));
	memcpy(&composite_config_descriptor_fs.interface, &interface_descriptor, sizeof(interface_descriptor));

	// Serial numnber.
	usb_set_descriptor_by_serial_number();
}
CALL_ON_INIT(set_up_descriptors);


/**
 *
 */
static usb_configuration_descriptor_t *configurations_hs[] = { (void *)&composite_config_descriptor_hs, 0 };
static usb_configuration_descriptor_t *configurations_fs[] = { (void *)&composite_config_descriptor_fs, 0 };


static usb_string_descriptor_t language_descriptor = {
	.length = sizeof(language_descriptor), .type   = USB_DESCRIPTOR_TYPE_STRING,
	.string = { 0x0409 }
};
static usb_string_descriptor_t manufacturer_string = {
	.length = sizeof(manufacturer_string), .type   = USB_DESCRIPTOR_TYPE_STRING,
	.string = u"Great Scott Gadgets",
};
static usb_string_descriptor_t product_string = {
	.length = sizeof(product_string), .type   = USB_DESCRIPTOR_TYPE_STRING,
	.string = u"GreatFET",
};


/**
 * ASCII versions of our string descriptors.
 */
static usb_string_descriptor_t *string_descriptors[] = {
	&language_descriptor,
	&manufacturer_string,
	&product_string,
	(usb_string_descriptor_t *)serial_number_string,
};



/**
 *  Initialize our USB peripherals.
 */
usb_peripheral_t usb_peripherals[] = {
	{
		.device_descriptor           = &device_descriptor,
		.string_descriptors          = string_descriptors,
		.language_descriptors        = &language_descriptor,
		.device_qualifier_descriptor = &device_qualifier_descriptor,
		.high_speed_configurations   = configurations_hs,
		.full_speed_configurations   = configurations_fs,

		// TODO: move these out of here and into the intialization routines
		.controller = 0,
		.active_configuration = NULL,
		.reg = USB0_REGISTER_BLOCK
	},
	{
		.device_descriptor           = &device_descriptor,
		.string_descriptors          = string_descriptors,
		.language_descriptors        = &language_descriptor,
		.device_qualifier_descriptor = &device_qualifier_descriptor,
		.high_speed_configurations   = configurations_hs,
		.full_speed_configurations   = configurations_fs,


		// TODO: move these out of here and into the intialization routines
		.controller = 1,
		.active_configuration = NULL,
		.reg = USB1_REGISTER_BLOCK
	}

};
