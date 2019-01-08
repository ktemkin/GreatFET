/*
 * This file is part of libgreat.
 * USB standard request handlers -- handles the standard portions of the USB request.
 */

#ifndef __USB_STANDARD_REQUEST_H__
#define __USB_STANDARD_REQUEST_H__

#include <drivers/usb/types.h>
#include "request.h"

usb_request_status_t usb_standard_request(usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage);

#endif//__USB_STANDARD_REQUEST_H__
