/*
 * This file is part of GreatFET
 */

#ifndef __USB_API_DS18B20_H__
#define __USB_API_DS18B20_H__

#include <drivers/usb/types.h>
#include <drivers/usb/request.h>

usb_request_status_t usb_vendor_request_DS18B20_read(
	usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage);

#endif /* __USB_API_DS18B20_H__ */
