/*
 * This file is part of libgreat.
 *
 * Non-HCI-specific HCI types.
 */

#include <stdint.h>
#include <stdbool.h>
#include <toolchain.h>

#include <drivers/usb/ehci/platform.h>

#ifndef __USB_TYPES_H__
#define __USB_TYPES_H__

/**
 * Stand-in for uchar.h
 */
typedef uint16_t char16_t;


/**
 * Generic opaque data structures.
 */
typedef struct usb_endpoint usb_endpoint_t;
typedef struct usb_peripheral usb_peripheral_t;


// Define the size of the host resources that should be preallocated.
enum {
	// Device mode constants.
	USB_TOTAL_QUEUE_HEADS = 12,

	// Host mode constants.
	USB_ASYNCHRONOUS_LIST_SIZE = 8,
	USB_PERIODIC_LIST_SIZE = 8,
	USB_TD_POOL_SIZE = 8,
};


// Maximum packet sizes for various USB speeds.
enum {
	USB_MAXIMUM_PACKET_SIZE_HIGH_SPEED = 512,
	USB_MAXIMUM_PACKET_SIZE_FULL_SPEED = 64,
};


//
// Data structures that are included in USB descriptors.
//

// USB device version
typedef union {
	uint16_t bcd;
	struct {
		uint8_t low_digit;
		uint8_t high_digit;
	};
} usb_bcd_version_t;


//
// Structures for each of the relevant USB descriptors:
//

/**
 * Structure describing an arbitrary USB descriptor.
 */
typedef struct {
	uint8_t length;
	uint8_t type;
	uint8_t data[];
} ATTR_PACKED usb_descriptor_t;


/**
 *  Device descriptor.
 */
typedef struct {
	uint8_t length;
	uint8_t type;

	// USB standard to which this device adheres.
	usb_bcd_version_t usb_version;

	// Information about the device's class, if applicable.
	// Describes if the device can be handled by standard drivers.
	uint8_t device_class;
	uint8_t device_subclass;
	uint8_t device_protocol;

	// The maximum packet size on the control endpoint.
	uint8_t ep0_max_packet_size;

	// Information that describes the device's identity.
	uint16_t vendor_id;
	uint16_t product_id;

	// Release version of the device.
	usb_bcd_version_t device_version;

	// String descriptors that help to identify the device to the user.
	uint8_t manufacturer_string_index;
	uint8_t product_string_index;
	uint8_t serial_string_index;

	// The number of total configurations.
	uint8_t configuration_count;

} ATTR_PACKED usb_device_descriptor_t;


/**
 * String descriptor.
 */
typedef struct {
	uint8_t length;
	uint8_t type;

	// The body of the relevant UTF-16/LE string.
	char16_t string[];

} ATTR_PACKED usb_string_descriptor_t;


/**
 * List elements for a sparse list of USB string descriptors.
 */
typedef struct {
	uint8_t index;
	usb_string_descriptor_t *descriptor;
} usb_string_descriptor_list_entry;


/**
 * Macros for simple defintions of USB string descriptors.
 */
#define USB_STRING_DESCRIPTOR_RAW(str, size) { \
	.length = sizeof(usb_string_descriptor_t) + size, \
	.type   = USB_DESCRIPTOR_TYPE_STRING, \
	.string = str \
}
#define USB_STRING_DESCRIPTOR(str)                 USB_STRING_DESCRIPTOR_RAW(u ## str, (sizeof(str) - 1) * 2)
#define USB_SUPPORTED_LANGUAGES_DESCRIPTOR(langs)  USB_STRING_DESCRIPTOR_RAW({ langs }, sizeof(uint16_t))

;
/**
 * Device qualifier descriptor -- describes information about how the device
 * would differ if it were operating in another speed. See 9.6.2 in the USB spec.
 */
typedef struct {
	uint8_t length;
	uint8_t type;

	// USB standard to which this device adheres.
	usb_bcd_version_t usb_version;

	// Information about the device's class, if applicable.
	// Describes if the device can be handled by standard drivers.
	uint8_t device_class;
	uint8_t device_subclass;
	uint8_t device_protocol;

	// The maximum packet size on the control endpoint.
	uint8_t ep0_max_packet_size;

	// The number of total configurations.
	uint8_t configuration_count;

	// For future use. (Spooky!)
	uint8_t reserved;

} ATTR_PACKED usb_device_qualifier_descriptor_t;


/**
 * Macro that allows us to specify our current draw.
 */
#define CURRENT_DRAW_IN_MILLIAMPS(x) (x >> 1)


/**
 * Structure describing a USB configuration.
 */
typedef struct {
	uint8_t length;
	uint8_t type;

	// A configuration descriptor can have attached subordinate descriptors.
	// Provide the total lengh of these descriptors.
	uint16_t total_length;

	// The total number of interfaces that belong to this configuration.
	uint8_t interface_count;

	// The "value" for this given configuration, which effecitvely is an
	// _non-zero_ index that identifies the given configuration.
	uint8_t value;

	// Index of the string that documents the configuration.
	uint8_t string_index;

	// The attributes array for the descriptor.
	struct {
		uint8_t               : 5;
		uint8_t remote_wakeup : 1;
		uint8_t self_powered  : 1;
		uint8_t bus_powered   : 1; // must always be set to one
	};

	// The maximum current draw in this configuration, in 2mA units--
	// so 50 = 100mA.
	uint8_t current_consumption;

} ATTR_PACKED usb_configuration_descriptor_t;


/**
 * Structure describing a USB configuration.
 */
typedef struct {
	uint8_t length;
	uint8_t type;

	// The interface number described by this descriptor.
	uint8_t number;

	// Alternate setting for this interface; if applicable.
	uint8_t alternate_setting;

	// The total number of endpoints that compose this interface.
	uint8_t endpoint_count;

	// Information about the device's class, if applicable.
	// Describes if the interface can be handled by standard drivers.
	//
	// Primarily used when the device's equivalent fields are all zero,
	// which indicates a composite device, where each interface can be bound
	// to its own driver.
	uint8_t device_class;
	uint8_t device_subclass;
	uint8_t device_protocol;

	// Index of the string that documents the interface.
	uint8_t string_index;

} ATTR_PACKED usb_interface_descriptor_t;


/**
 * Structure describing a USB endpoint.
 */
typedef struct {
	uint8_t length;
	uint8_t type;

	// The endpoint's address attributes.
	union {
		struct {
			uint8_t number    : 4;
			uint8_t           : 3;
			uint8_t direction : 1;
		};
		uint8_t address;
	};

	// The core properties of the endpoint.
	struct {
		uint8_t transfer_type        : 2;
		uint8_t synchronization_type : 2;
		uint8_t usage_type           : 2;
		uint8_t reserved             : 2;
	};

	// The largest amount of data that can be fit into a
	// packet.
	uint16_t max_packet_size;

	// For periodic endpoints (interrupt/isochronous), information
	// about the
	uint8_t interval;


} ATTR_PACKED usb_endpoint_descriptor_t;


/**
 * Structure that describes a USB setup packet.
 */
typedef struct {
	union {
		uint8_t request_type;
		struct {
			uint32_t direction : 1;
			uint32_t type : 2;
			uint32_t recipient : 5;
		};
	};

	uint8_t request;
	union {
		uint16_t value;
		struct {
			uint8_t value_l;
			uint8_t value_h;
		};
	};
	union {
		uint16_t index;
		struct {
			uint8_t index_l;
			uint8_t index_h;
		};
	};
	union {
		uint16_t length;
		struct {
			uint8_t length_l;
			uint8_t length_h;
		};
	};
} ATTR_PACKED usb_setup_t;

/**
 * Numbers for the standard USB requests.
 */
typedef enum {
	USB_STANDARD_REQUEST_GET_STATUS = 0,
	USB_STANDARD_REQUEST_CLEAR_FEATURE = 1,
	USB_STANDARD_REQUEST_SET_FEATURE = 3,
	USB_STANDARD_REQUEST_SET_ADDRESS = 5,
	USB_STANDARD_REQUEST_GET_DESCRIPTOR = 6,
	USB_STANDARD_REQUEST_SET_DESCRIPTOR = 7,
	USB_STANDARD_REQUEST_GET_CONFIGURATION = 8,
	USB_STANDARD_REQUEST_SET_CONFIGURATION = 9,
	USB_STANDARD_REQUEST_GET_INTERFACE = 10,
	USB_STANDARD_REQUEST_SET_INTERFACE = 11,
	USB_STANDARD_REQUEST_SYNCH_FRAME = 12,
} usb_standard_request_t;

typedef enum {
	USB_SETUP_REQUEST_RECIPIENT_mask = 0x1F,
	USB_SETUP_REQUEST_RECIPIENT_DEVICE = 0,
	USB_SETUP_REQUEST_RECIPIENT_INTERFACE = 1,
	USB_SETUP_REQUEST_RECIPIENT_ENDPOINT = 2,
	USB_SETUP_REQUEST_RECIPIENT_OTHER = 3,

	USB_SETUP_REQUEST_TYPE_shift = 5,
	USB_SETUP_REQUEST_TYPE_mask = 3 << USB_SETUP_REQUEST_TYPE_shift,

	USB_SETUP_REQUEST_TYPE_STANDARD = 0 << USB_SETUP_REQUEST_TYPE_shift,
	USB_SETUP_REQUEST_TYPE_CLASS = 1 << USB_SETUP_REQUEST_TYPE_shift,
	USB_SETUP_REQUEST_TYPE_VENDOR = 2 << USB_SETUP_REQUEST_TYPE_shift,
	USB_SETUP_REQUEST_TYPE_RESERVED = 3 << USB_SETUP_REQUEST_TYPE_shift,

	USB_SETUP_REQUEST_TYPE_DATA_TRANSFER_DIRECTION_shift = 7,
	USB_SETUP_REQUEST_TYPE_DATA_TRANSFER_DIRECTION_mask = 1 << USB_SETUP_REQUEST_TYPE_DATA_TRANSFER_DIRECTION_shift,
	USB_SETUP_REQUEST_TYPE_DATA_TRANSFER_DIRECTION_HOST_TO_DEVICE = 0 << USB_SETUP_REQUEST_TYPE_DATA_TRANSFER_DIRECTION_shift,
	USB_SETUP_REQUEST_TYPE_DATA_TRANSFER_DIRECTION_DEVICE_TO_HOST = 1 << USB_SETUP_REQUEST_TYPE_DATA_TRANSFER_DIRECTION_shift,
} usb_setup_request_type_t;

typedef enum {
	USB_TRANSFER_DIRECTION_OUT = 0,
	USB_TRANSFER_DIRECTION_IN = 1,
} usb_transfer_direction_t;

typedef enum {
	USB_DESCRIPTOR_TYPE_DEVICE = 1,
	USB_DESCRIPTOR_TYPE_CONFIGURATION = 2,
	USB_DESCRIPTOR_TYPE_STRING = 3,
	USB_DESCRIPTOR_TYPE_INTERFACE = 4,
	USB_DESCRIPTOR_TYPE_ENDPOINT = 5,
	USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER = 6,
	USB_DESCRIPTOR_TYPE_OTHER_SPEED_CONFIGURATION = 7,
	USB_DESCRIPTOR_TYPE_INTERFACE_POWER = 8,
} usb_descriptor_type_t;

typedef enum {
	USB_TRANSFER_TYPE_CONTROL = 0,
	USB_TRANSFER_TYPE_ISOCHRONOUS = 1,
	USB_TRANSFER_TYPE_BULK = 2,
	USB_TRANSFER_TYPE_INTERRUPT = 3,
} usb_transfer_type_t;

typedef enum {
	USB_SPEED_LOW = 0,
	USB_SPEED_FULL = 1,
	USB_SPEED_HIGH = 2,
	USB_SPEED_SUPER = 3,
} usb_speed_t;

typedef enum {
  USB_CONTROLLER_MODE_DEVICE = 0,
  USB_CONTROLLER_MODE_HOST = 1
} usb_controller_mode_t;

typedef enum {
	USB_PID_TOKEN_OUT   = 0,
	USB_PID_TOKEN_IN    = 1,
	USB_PID_TOKEN_SETUP = 2
} usb_token_t;


/**
 * Structure representing a USB endpoint, from the driver's perspective.
 */
struct usb_endpoint {
	usb_setup_t setup;

	// The endpoint's address attributes.
	union {
		struct {
			uint8_t number    : 7;
			uint8_t direction : 1;
		};
		uint8_t address;
	};

	usb_peripheral_t *device;

	usb_endpoint_t *in;
	usb_endpoint_t *out;

	void (*setup_complete)(usb_endpoint_t* const endpoint);
	void (*transfer_complete)(usb_endpoint_t* const endpoint);
};


#endif
