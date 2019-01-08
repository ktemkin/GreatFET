/*
 * This file is part of GreatFET
 */

#ifndef __USB_DEVICE_QUEUE_H__
#define __USB_DEVICE_QUEUE_H__




typedef struct _usb_transfer_t usb_transfer_t;
typedef struct _usb_queue_t usb_queue_t;

typedef struct usb_endpoint usb_endpoint_t;


typedef void (*transfer_completion_cb)(void*, unsigned int);


typedef struct usb_transfer_descriptor_t usb_transfer_descriptor_t;
struct usb_transfer_descriptor_t {
	volatile usb_transfer_descriptor_t *next_dtd_pointer;
	volatile uint32_t total_bytes;
	volatile uint32_t buffer_pointer_page[5];
	volatile uint32_t _reserved;
};


typedef struct {
	volatile uint32_t capabilities;
	volatile usb_transfer_descriptor_t *current_dtd_pointer;
	volatile usb_transfer_descriptor_t *next_dtd_pointer;
	volatile uint32_t total_bytes;
	volatile uint32_t buffer_pointer_page[5];
	volatile uint32_t _reserved_0;
	volatile uint8_t setup[8];
	volatile uint32_t _reserved_1[4];
} usb_queue_head_t;


// This is an opaque datatype. Thou shall not touch these members.
struct _usb_transfer_t {
        struct _usb_transfer_t* next;
        usb_transfer_descriptor_t td ATTR_ALIGNED(64);
        unsigned int maximum_length;
        struct _usb_queue_t* queue;
        transfer_completion_cb completion_cb;
        void* user_data;
};

// This is an opaque datatype. Thou shall not touch these members.
struct _usb_queue_t {
        struct usb_endpoint_t* endpoint;
        const unsigned int pool_size;
        usb_transfer_t* volatile free_transfers;
        usb_transfer_t* volatile active;
};

#define USB_DECLARE_QUEUE(endpoint_name)                                \
        struct _usb_queue_t endpoint_name##_queue;
#define USB_DEFINE_QUEUE(endpoint_name, _pool_size)                     \
        struct _usb_transfer_t endpoint_name##_transfers[_pool_size];   \
        struct _usb_queue_t endpoint_name##_queue = {                   \
                .endpoint = &endpoint_name,                             \
                .free_transfers = endpoint_name##_transfers,            \
                .pool_size = _pool_size                                 \
        };

void usb_queue_flush_endpoint(const usb_endpoint_t* const endpoint);

int usb_transfer_schedule(
	const usb_endpoint_t* const endpoint,
	void* const data,
	const uint32_t maximum_length,
        const transfer_completion_cb completion_cb,
        void* const user_data
);

int usb_transfer_schedule_block(
	const usb_endpoint_t* const endpoint,
	void* const data,
	const uint32_t maximum_length,
        const transfer_completion_cb completion_cb,
        void* const user_data
);

int usb_transfer_schedule_wait(
	const usb_endpoint_t* const endpoint,
	void* const data,
	const uint32_t maximum_length,
	const transfer_completion_cb completion_cb,
	void* const user_data,
	uint32_t timeout
);

int usb_transfer_schedule_ack(
	const usb_endpoint_t* const endpoint
);

void usb_queue_init(
        usb_queue_t* const queue
);

void usb_queue_transfer_complete(
        usb_endpoint_t* const endpoint
);


void usb_queue_invalidate_transfers(
        usb_endpoint_t* const endpoint
);
#endif//__USB_QUEUE_H__
