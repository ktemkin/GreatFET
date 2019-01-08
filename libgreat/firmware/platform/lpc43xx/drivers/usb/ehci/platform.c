/*
 * This file is part of libgreat.
 *
 * Generic USB drivers for EHCI and Simplified EHCI host and device controllers.
 */


/*
 * @return the index of the endpoints QH in the controller's data structure
 * given the endpoint's number and diretion.
 */
static inline int _endpoint_index_for_endpoint_number(uint8_t ep_number, bool is_in)
{
    return (ep_number << 1) | (is_in ? 1 : 0);
}


/**
 * @return the index of the endpoints QH in the controller's data structure
 * given the endpoint address.
 */
static inline int _endpoint_index_for_address(uint8_t ep_address)
{
    return _endpoint_index_for_endpoint_number(ep_address & 0x7f, ep_address & 0x80);
}


/**
 * Fetches the Queue Head for the given endpoint.
 */
usb_queue_head_t* usb_qh_for_endpoint(usb_peripheral_t *device, uint8_t endpoint_address)
{
	int qh_index = _endpoint_index_for_address(endpoint_address);
	return &device->device_platform.queue_heads_device[qh_index];
}

