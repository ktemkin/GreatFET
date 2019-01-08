/*
 * This file is part of libgreat.
 *
 * Generic USB drivers for EHCI and Simplified EHCI host and device controllers.
 */

#include <stdint.h>
#include <stdbool.h>
#include <debug.h>

#include <drivers/usb/types.h>
#include <drivers/usb/standard_request.h>

#include <drivers/usb/ehci/device.h>
#include <drivers/usb/ehci/types.h>

#include <drivers/usb/ehci/device_queue.h>
#include <drivers/usb/ehci/registers.h>
#include <drivers/usb/ehci/platform.h>

#include "greatfet_core.h"

#include <libopencm3/lpc43xx/creg.h>
#include <libopencm3/lpc43xx/m4/nvic.h>
#include <libopencm3/lpc43xx/rgu.h>
#include <libopencm3/lpc43xx/scu.h>


usb_endpoint_t* usb_endpoint_from_address(const uint_fast8_t endpoint_address, usb_peripheral_t* const device)
{
	return (usb_endpoint_t*)usb_queue_head(endpoint_address, device)->_reserved_0;
}


uint_fast8_t usb_endpoint_address(const usb_transfer_direction_t direction, const uint_fast8_t number)
{
	return ((direction == USB_TRANSFER_DIRECTION_IN) ? 0x80 : 0x00) + number;
}


static bool usb_endpoint_is_in(const uint_fast8_t endpoint_address)
{
	return (endpoint_address & 0x80) ? true : false;
}


static uint_fast8_t usb_endpoint_number(const uint_fast8_t endpoint_address)
{
	return (endpoint_address & 0xF);
}


void usb_peripheral_reset(const usb_peripheral_t* const device)
{
	uint32_t mask = device->controller ? RESET_CTRL0_USB1_RST : RESET_CTRL0_USB0_RST;

	// Trigger the reset, and wait for it to pass.
	RESET_CTRL0 = mask;
	RESET_CTRL0 = 0;
	while( (RESET_ACTIVE_STATUS0 & mask) == 0 );
}


void usb_phy_enable(const usb_peripheral_t* const device) {
	if(device->controller == 0) {
		CREG_CREG0 &= ~CREG_CREG0_USB0PHY;
	}
	if(device->controller == 1) {
		/* Enable the USB1 FS PHY. */
		SCU_SFSUSB = 0x12;

#ifdef BOARD_CAPABILITY_USB1_SENSE_VBUS
		/*
		 * HACK: The USB1 PHY will only run if we tell it VBUS is
		 * present by setting SFSUSB bit 5. Shortly, we should use
		 * the USB1_SENSE pin to drive an interrupt that adjusts this
		 * bit to match the sense pin's value. For now, we'll lie and
		 * say VBUS is always there.
		 */
		SCU_SFSUSB |= (1 << 5);
#else
		/*
		 * If we don't have the ability to sense VBUS, lie and pretend that we
		 * always detect it. This actually works pretty perfectly for pretty much
		 * all USB hosts, even if it's in violation of the spec-- which says we
		 * shouldn't drive current through D+/D- until VBUS is present.
		 */
		SCU_SFSUSB |= (1 << 5);
#endif
	}
}


static void usb_clear_pending_interrupts(const uint32_t mask, const usb_peripheral_t* const device)
{
	device->reg->endptnak = mask;
	device->reg->endptnaken = mask;
	device->reg->usbsts.all = mask;
	device->reg->endptsetupstat = device->reg->endptsetupstat & mask;
	device->reg->endptcomplete = device->reg->endptcomplete & mask;
}


static void usb_clear_all_pending_interrupts(const usb_peripheral_t* const device)
{
	usb_clear_pending_interrupts(0xFFFFFFFF, device);
}


static void usb_wait_for_endpoint_priming_to_finish(const uint32_t mask, const usb_peripheral_t* const device)
{
	// Wait until controller has parsed new transfer descriptors and prepared
	// receive buffers. // TODO: support timeout?
	while (device->reg->endptprime & mask);
}


static void usb_flush_endpoints(const uint32_t mask, const usb_peripheral_t* const device)
{
	// Clear any primed buffers. If a packet is in progress, that transfer
	// will continue until completion.
	device->reg->endptflush = mask;
}


static void usb_wait_for_endpoint_flushing_to_finish(const uint32_t mask, const usb_peripheral_t* const device)
{
	// Wait until controller has flushed all endpoints / cleared any primed
	// buffers.
	while (device->reg->endptflush & mask);
}


static void usb_flush_primed_endpoints(const uint32_t mask, const usb_peripheral_t* const device)
{
	usb_wait_for_endpoint_priming_to_finish(mask, device);
	usb_flush_endpoints(mask, device);
	usb_wait_for_endpoint_flushing_to_finish(mask, device);

	// TODO: toggle reclamation of any TDs?
}


static void usb_flush_all_primed_endpoints(const usb_peripheral_t* const device)
{
	usb_flush_primed_endpoints(0xFFFFFFFF, device);
}


static void usb_endpoint_set_type(const usb_endpoint_t* const endpoint, const usb_transfer_type_t transfer_type)
{
	volatile usb_register_block_t *registers = endpoint->device->reg;

	// NOTE: UM10503 section 23.6.24 "Endpoint 1 to 5 control registers" says
	// that the disabled side of an endpoint must be set to a non-control type
	// (e.g. bulk, interrupt, or iso).
	const uint_fast8_t endpoint_number = usb_endpoint_number(endpoint->address);
	registers->endptctrl[endpoint_number]
		= ( registers->endptctrl[endpoint_number] & ~(USB0_ENDPTCTRL_TXT1_0_MASK | USB0_ENDPTCTRL_RXT_MASK))
				| ( USB0_ENDPTCTRL_TXT1_0(transfer_type) | USB0_ENDPTCTRL_RXT(transfer_type) );
}


static void usb_endpoint_enable(const usb_endpoint_t* const endpoint)
{
	const uint_fast8_t endpoint_number = usb_endpoint_number(endpoint->address);
	volatile usb_register_block_t *registers = endpoint->device->reg;

	registers->endptctrl[endpoint_number] |= usb_endpoint_is_in(endpoint->address) ?
		(USB0_ENDPTCTRL_TXE | USB0_ENDPTCTRL_TXR) :(USB0_ENDPTCTRL_RXE | USB0_ENDPTCTRL_RXR);
}


static void usb_endpoint_clear_pending_interrupts(const usb_endpoint_t* const endpoint)
{
	const uint_fast8_t endpoint_number = usb_endpoint_number(endpoint->address);
	uint32_t to_clear = usb_endpoint_is_in(endpoint->address) ?
		USB0_ENDPTCOMPLETE_ETCE(1 << endpoint_number) : USB0_ENDPTCOMPLETE_ERCE(1 << endpoint_number);
	usb_clear_pending_interrupts(to_clear, endpoint->device);
}


void usb_endpoint_disable(const usb_endpoint_t* const endpoint)
{
	const uint_fast8_t endpoint_number = usb_endpoint_number(endpoint->address);
	volatile usb_register_block_t *registers = endpoint->device->reg;

	// Disable the endpoint...
	uint32_t mask = usb_endpoint_is_in(endpoint->address) ? USB0_ENDPTCTRL_TXE : USB0_ENDPTCTRL_RXE;
	registers->endptctrl[endpoint_number] &= ~(mask);

	// .. and clear any pending transfers.
	usb_queue_flush_endpoint(endpoint);
	usb_endpoint_clear_pending_interrupts(endpoint);
	usb_endpoint_flush(endpoint);
}


void usb_endpoint_prime(const usb_endpoint_t* const endpoint, usb_transfer_descriptor_t* const first_td)
{
	usb_queue_head_t* const qh = usb_queue_head(endpoint->address, endpoint->device);
	const uint_fast8_t endpoint_number = usb_endpoint_number(endpoint->address);
	volatile usb_register_block_t *registers = endpoint->device->reg;

	uint32_t prime_mask = usb_endpoint_is_in(endpoint->address) ?
		USB0_ENDPTPRIME_PETB(1 << endpoint_number) : USB0_ENDPTPRIME_PERB(1 << endpoint_number);

	// Register the transfer descriptor in the endpoint's queue head...
	qh->next_dtd_pointer = first_td;
	qh->total_bytes &= ~(USB_TD_DTD_TOKEN_STATUS_ACTIVE | USB_TD_DTD_TOKEN_STATUS_HALTED);

	// ... and notify the controller that we've added to the QH.
	registers->endptprime = prime_mask;
}


static bool usb_endpoint_is_priming(const usb_endpoint_t* const endpoint)
{
	const uint_fast8_t endpoint_number = usb_endpoint_number(endpoint->address);
	volatile usb_register_block_t *registers = endpoint->device->reg;

	uint32_t prime_mask = usb_endpoint_is_in(endpoint->address) ?
		USB0_ENDPTPRIME_PETB(1 << endpoint_number) : USB0_ENDPTPRIME_PERB(1 << endpoint_number);

	return registers->endptprime & prime_mask;
}


// Schedule an already filled-in transfer descriptor for execution on
// the given endpoint, waiting until the endpoint has finished.
void usb_endpoint_schedule_wait(const usb_endpoint_t* const endpoint, usb_transfer_descriptor_t* const td)
{
	// Ensure that endpoint is ready to be primed.
	// It may have been flushed due to an aborted transaction.
	// TODO: This should be preceded by a flush?
	while (usb_endpoint_is_ready(endpoint));

	td->next_dtd_pointer = USB_TD_NEXT_DTD_POINTER_TERMINATE;
	usb_endpoint_prime(endpoint, td);
}


// Schedule an already filled-in transfer descriptor for execution on
// the given endpoint, appending to the end of the endpoint's queue if
// there are pending TDs. Note that this requires that one knows the
// tail of the endpoint's TD queue. Moreover, the user is responsible
// for setting the TERMINATE bit of next_dtd_pointer if needed.
void usb_endpoint_schedule_append(const usb_endpoint_t* const endpoint,
		usb_transfer_descriptor_t* const tail_td, usb_transfer_descriptor_t* const new_td)
{
	volatile usb_register_block_t *registers = endpoint->device->reg;

	bool done = 0;
	tail_td->next_dtd_pointer = new_td;

	// FIXME: the name of this function is misleading; this is really more post-append,
	// since we always previously append to the tail TD.
	if (usb_endpoint_is_priming(endpoint)) {
		return;
	}

	// TODO: document this
	do {
		registers->usbcmd |= USB0_USBCMD_D_ATDTW;
		done = usb_endpoint_is_ready(endpoint);
	}
	while (!(registers->usbcmd & USB0_USBCMD_D_ATDTW));

	registers->usbcmd &= ~USB0_USBCMD_D_ATDTW;

	if(!done) {
		usb_endpoint_prime(endpoint, new_td);
	}
}


void usb_endpoint_flush(const usb_endpoint_t* const endpoint)
{
	const uint_fast8_t endpoint_number = usb_endpoint_number(endpoint->address);
	usb_queue_flush_endpoint(endpoint);

	uint32_t flush_mask = usb_endpoint_is_in(endpoint->address) ?
		USB0_ENDPTFLUSH_FETB(1 << endpoint_number) : USB0_ENDPTFLUSH_FERB(1 << endpoint_number);

	usb_flush_primed_endpoints(flush_mask, endpoint->device);
}


bool usb_endpoint_is_ready(const usb_endpoint_t* const endpoint)
{
	const uint_fast8_t endpoint_number = usb_endpoint_number(endpoint->address);
	volatile usb_register_block_t *registers = endpoint->device->reg;

	uint32_t ready_mask = usb_endpoint_is_in(endpoint->address) ?
		USB0_ENDPTSTAT_ETBR(1 << endpoint_number) : USB0_ENDPTSTAT_ERBR(1 << endpoint_number);

	return registers->endptstat & ready_mask;
}


bool usb_endpoint_is_complete(const usb_endpoint_t* const endpoint)
{
	const uint_fast8_t endpoint_number = usb_endpoint_number(endpoint->address);
	volatile usb_register_block_t *registers = endpoint->device->reg;

	uint32_t complete_mask = usb_endpoint_is_in(endpoint->address) ?
		USB0_ENDPTCOMPLETE_ETCE(1 << endpoint_number) : USB0_ENDPTCOMPLETE_ERCE(1 << endpoint_number);

	return registers->endptcomplete & complete_mask;
}


void usb_endpoint_stall(const usb_endpoint_t* const endpoint)
{
	const uint_fast8_t endpoint_number = usb_endpoint_number(endpoint->address);

	// Endpoint is to be stalled as a pair -- both OUT and IN.
	// See UM10503 section 23.10.5.2 "Stalling"
	endpoint->device->reg->endptctrl[endpoint_number] |= (USB0_ENDPTCTRL_RXS | USB0_ENDPTCTRL_TXS);

	// If this is a protocol stall (a stall on a control endpoint),
	// clear out any allocated TDs.
	if(endpoint_number == 0) {
		usb_endpoint_flush(endpoint->in);
		usb_endpoint_flush(endpoint->out);
	}
}


void usb_controller_run(const usb_peripheral_t* const device)
{
	device->reg->usbcmd |= USB0_USBCMD_D_RS;
}


static void usb_controller_stop(const usb_peripheral_t* const device)
{
	device->reg->usbcmd &= ~USB0_USBCMD_D_RS;
}


static uint_fast8_t usb_controller_is_resetting(const usb_peripheral_t* const device)
{
	return (device->reg->usbcmd & USB0_USBCMD_D_RST) != 0;
}


/**
 * If we don't have an implementation of USB host, we don't need to
 * disable any pull-downs, as we assume they were never turned on.
 *
 * This is weak, and only used if USB host tools aren't included when
 * building.
 */
void WEAK usb_host_disable_pulldowns(usb_peripheral_t *device)
{
	(void)device;
}


static void usb_controller_set_device_mode(usb_peripheral_t* device)
{

	// Mark the peripheral as in DEVICE mode.
	device->mode = USB_CONTROLLER_MODE_DEVICE;

	// And disable any host-mode pull-downs used.
	usb_host_disable_pulldowns(device);

	// Set USB device mode
	device->reg->usbmode = USB0_USBMODE_D_CM1_0(2);

	// If this is the USB1 port, set the OTG-related termination.
	if( device->controller == 0) {
		// Set device-related OTG flags
		// OTG termination: controls pull-down on USB_DM
		device->reg->otgsc = USB0_OTGSC_OT;
	}
}


usb_speed_t usb_current_device_speed(const usb_peripheral_t* const device)
{
	switch (device->reg->portsc1.all & USB0_PORTSC1_D_PSPD_MASK)
	{
		case USB0_PORTSC1_D_PSPD(0):
			return USB_SPEED_FULL;

		case USB0_PORTSC1_D_PSPD(2):
			return USB_SPEED_HIGH;

		default:
			// TODO: What to do/return here? Is this even possible?
			pr_warning("USB: Unexpected USB port speed detected! Defaulting to full...\n");
			return USB_SPEED_FULL;
	}
}


usb_interrupt_flags_t usb_get_status(const usb_peripheral_t* const device)
{
	usb_interrupt_flags_t status;

	// Read the status of the activated interrupts...
	status.all =  device->reg->usbsts.all & device->reg->usbintr.all;

	// Clear flags that were just read, leaving alone any flags that
	// were just set (after the read). It's important to read and
	// reset flags atomically! :-)
	device->reg->usbsts.all = status.all;
	return status;
}


void usb_clear_endpoint_setup_status(const uint32_t endpoint_setup_status,
		const usb_peripheral_t* const device)
{
	// Clear the Setup ready, and wait for the clear to complete.
	device->reg->endptsetupstat = endpoint_setup_status;
	while (device->reg->endptsetupstat & endpoint_setup_status);
}


uint32_t usb_get_endpoint_setup_status(const usb_peripheral_t* const device)
{
	return device->reg->endptsetupstat;
}


void usb_clear_endpoint_complete(const uint32_t endpoint_complete, const usb_peripheral_t* const device)
{
	device->reg->endptcomplete = endpoint_complete;
}


uint32_t usb_get_endpoint_complete(const usb_peripheral_t* const device)
{
	return device->reg->endptcomplete;
}


uint32_t usb_get_endpoint_ready(const usb_peripheral_t* const device)
{
	return device->reg->endptstat;
}

static void usb_disable_all_endpoints(const usb_peripheral_t* const device)
{
	// Endpoint 0 is always enabled. TODO: So why set ENDPTCTRL0?
	for (int i = 0; i < 6; ++i) {
		device->reg->endptctrl[i] &= ~(USB0_ENDPTCTRL0_RXE | USB0_ENDPTCTRL0_TXE);
	}
}

void usb_set_address_immediate(const usb_peripheral_t* const device, const uint_fast8_t address)
{
	device->reg->deviceaddr = USB0_DEVICEADDR_USBADR(address);
}


void usb_set_address_deferred(const usb_peripheral_t* const device, const uint_fast8_t address)
{
	device->reg->deviceaddr = USB0_DEVICEADDR_USBADR(address) | USB0_DEVICEADDR_USBADRA;
}


static void usb_reset_all_endpoints(const usb_peripheral_t* const device)
{
	usb_disable_all_endpoints(device);
	usb_clear_all_pending_interrupts(device);
	usb_flush_all_primed_endpoints(device);
}


void usb_controller_reset(usb_peripheral_t* const device)
{
	// TODO: Good to disable some USB interrupts to avoid priming new
	// new endpoints before the controller is reset?
	usb_reset_all_endpoints(device);
	usb_controller_stop(device);

	// Reset controller. Resets internal pipelines, timers, counters, state
	// machines to initial values. Not recommended when device is in attached
	// state -- effect on attached host is undefined. Detach first by flushing
	// all primed endpoints and stopping controller.
	device->reg->usbcmd = USB0_USBCMD_D_RST;
	while(usb_controller_is_resetting(device));
}


/**
 * Handles a host-issued USB bus reset -- effectively setting up the device controller
 * for a new burst of communications.
 */
void usb_handle_bus_reset(usb_peripheral_t *const device)
{
	// According to UM10503 v1.4 section 23.10.3 "Bus reset":
	usb_reset_all_endpoints(device);
	usb_set_address_immediate(device, 0);
	usb_set_configuration(device, 0);
}


void usb_set_irq_handler(usb_peripheral_t* const device, vector_table_entry_t isr)
{
	int irq_number = device->controller ? NVIC_USB1_IRQ : NVIC_USB0_IRQ;
	vector_table.irq[irq_number] = isr;
}


static void usb_interrupt_enable(usb_peripheral_t* const device)
{
	int irq_number = device->controller ? NVIC_USB1_IRQ : NVIC_USB0_IRQ;
	nvic_enable_irq(irq_number);
}


/**
 * Provides a reference to the pool of all configurations associated with
 * the device's current speed.
 */
static usb_configuration_descriptor_t **usb_current_configuration_pool(usb_peripheral_t *device)
{
	// Select the pool of configuration descriptors we're working with
	if (usb_current_device_speed(device) == USB_SPEED_HIGH) {
		return device->high_speed_configurations;
	} else {
		return device->full_speed_configurations;
	}
}


/**
 * @returns a reference to the pool of all configurations associated with
 * the device's current speed.
 */
static usb_configuration_descriptor_t **usb_other_configuration_pool(usb_peripheral_t *device)
{
	// Select the pool of configuration descriptors we're working with
	if (usb_current_device_speed(device) == USB_SPEED_HIGH) {
		return device->full_speed_configurations;
	} else {
		return device->high_speed_configurations;
	}
}

/**
 * Finds the configuration descriptor associated with the given value.
 *
 * @param configuration_value The value to search for a descriptor for.
 * @return A pointer to the configuration descriptor, or NULL if none could be found.
 */
static usb_configuration_descriptor_t * _usb_find_configuration_descriptor(
		usb_peripheral_t *device, uint8_t configuration_value, bool is_other_speed)
{
	usb_device_descriptor_t *device_descriptor = device->device_descriptor;
	usb_configuration_descriptor_t **configurations;

	// If we're looking for configurations for the non-active pool, grab that pool.
	// Otherwise, use the current pool of configurations.
	if (is_other_speed) {
		configurations = usb_other_configuration_pool(device);
	} else {
		configurations = usb_current_configuration_pool(device);
	}

	// A configuration value of "0" indicates an unconfigured device; and accoridingly
	// has no descriptor.
	if (!configuration_value) {
		return NULL;
	}

	// If the device doesn't have any known configurations, we must not have any
	// descriptors.  Fail out.
	if (!configurations) {
		return NULL;
	}

	// Iterate through each of the configurations possible for the current device speed.
	for (int i = 0; i < device_descriptor->configuration_count; ++i) {
		usb_configuration_descriptor_t *config = configurations[i];

		// If we've found the relevant configuration, return it.
		if (config->value == configuration_value) {
			return config;
		}
	}

	return NULL;
}


/**
 * Finds the configuration descriptor associated with the given value.
 *
 * @param configuration_value The value to search for a descriptor for.
 * @return A pointer to the configuration descriptor, or NULL if none could be found.
 */
usb_configuration_descriptor_t * usb_find_configuration_descriptor(
		usb_peripheral_t *device, uint8_t configuration_value) {
	return _usb_find_configuration_descriptor(device, configuration_value, false);
}


/**
 * Finds the configuration descriptor associated with the given value.
 *
 * @param configuration_value The value to search for a descriptor for.
 * @return A pointer to the configuration descriptor, or NULL if none could be found.
 */
usb_configuration_descriptor_t * usb_find_other_speed_configuration_descriptor(
		usb_peripheral_t *device, uint8_t configuration_value) {
	return _usb_find_configuration_descriptor(device, configuration_value, true);
}


/**
 * Apply a given configuration to the USB device.
 *
 * @param configuration_value The configuration value for the given configuration,
 *		as denoted in the relevant configuration descriptor, or 0 to de-configure the device.
 *
 * @return 0 on success, or an error code on failure.
 */
int usb_set_configuration(usb_peripheral_t *device, uint8_t configuration_value)
{
	// Try to find the configuration descriptor for the relevant configuration.
	usb_configuration_descriptor_t *new_configuration =
		usb_find_configuration_descriptor(device, configuration_value);

	// If we're trying to apply a configuration, and we couldn't find a relevant
	// descriptor, fail out.
	if (configuration_value && !new_configuration) {
		return EFAULT;
	}

	// If the configuration's changed, store it.
	device->active_configuration = new_configuration;

	// FIXME: apply our endpoint configuration per this configuration

	// If the device has registered a callback
	if (device->configuration_changed_callback) {
		device->configuration_changed_callback(device);
	}

	return 0;
}


void usb_device_init(usb_peripheral_t* const device)
{
	usb_phy_enable(device);
	usb_controller_reset(device);
	usb_controller_set_device_mode(device);

	// Set interrupt threshold interval to 0
	device->reg->usbcmd &= ~USB0_USBCMD_D_ITC_MASK;

	// Configure endpoint list address
	device->reg->endpointlistaddr = (uint32_t)&device->queue_heads_device;

	// Enable interrupts
	device->reg->usbintr.all =
		  USB0_USBINTR_D_UE
		| USB0_USBINTR_D_UEE
		| USB0_USBINTR_D_PCE
		| USB0_USBINTR_D_URE
		//| USB0_USBINTR_D_SRE
		| USB0_USBINTR_D_SLE
		| USB0_USBINTR_D_NAKE
		;
}

/**
 * Disables the ability for the given port to connect at high speed.
 *
 * Useful for debugging high-speed-specific modes or viewing things
 * with more primitive USB analyzers.
 */
void usb_prevent_high_speed(usb_peripheral_t *device)
{
	pr_warning("USB: warning: disabling high speed communications at fw request! \n");
	device->reg->portsc1.force_full_speed = true;
}


/**
 * Cancels the effects of a previous usb_prevent_high_speed(), re-enabling
 * the abiltity for a device to connect at high speeds.
 */
void usb_allow_high_speed(usb_peripheral_t *device)
{
	pr_warning("USB: re-enabling high speed communciations\n");
	device->reg->portsc1.force_full_speed = false;
}


void usb_run(usb_peripheral_t* const device)
{
	usb_interrupt_enable(device);
	usb_controller_run(device);
}


void usb_copy_setup(usb_setup_t* const dst, const volatile uint8_t* const src)
{
	dst->request_type = src[0];
	dst->request = src[1];
	dst->value_l = src[2];
	dst->value_h = src[3];
	dst->index_l = src[4];
	dst->index_h = src[5];
	dst->length_l = src[6];
	dst->length_h = src[7];
}

/**
 * Configure the simplified queue head for a given endpoint.
 */
void usb_configure_endpoint_queue_head(usb_endpoint_t *endpoint,
		uint16_t max_packet_size, usb_transfer_type_t transfer_type)
{
	usb_endpoint_flush(endpoint);

	// TODO: There are more capabilities to adjust based on the endpoint
	// descriptor.
	usb_queue_head_t* const qh = usb_queue_head(endpoint->address, endpoint->device);
	qh->capabilities
		= USB_QH_CAPABILITIES_MULT(0)
		| USB_QH_CAPABILITIES_MPL(max_packet_size)
		| ((transfer_type == USB_TRANSFER_TYPE_CONTROL) ? USB_QH_CAPABILITIES_IOS : 0)
		| ((transfer_type == USB_TRANSFER_TYPE_CONTROL) ? 0 : USB_QH_CAPABILITIES_ZLT);
	qh->current_dtd_pointer = 0;
	qh->next_dtd_pointer = USB_TD_NEXT_DTD_POINTER_TERMINATE;
	qh->total_bytes
		= USB_TD_DTD_TOKEN_TOTAL_BYTES(0)
		| USB_TD_DTD_TOKEN_MULTO(0)
		;
	qh->buffer_pointer_page[0] = 0;
	qh->buffer_pointer_page[1] = 0;
	qh->buffer_pointer_page[2] = 0;
	qh->buffer_pointer_page[3] = 0;
	qh->buffer_pointer_page[4] = 0;

	// This is how we look up an endpoint structure from an endpoint address:
	qh->_reserved_0 = (uint32_t)endpoint;

	usb_endpoint_set_type(endpoint, transfer_type);

	usb_endpoint_enable(endpoint);
}


void usb_in_endpoint_enable_nak_interrupt(const usb_endpoint_t* const endpoint)
{
	uint8_t endpoint_number = usb_endpoint_number(endpoint->address);
	endpoint->device->reg->endptnaken |= USB0_ENDPTNAKEN_EPTNE(1 << endpoint_number);
}


void usb_in_endpoint_disable_nak_interrupt(const usb_endpoint_t* const endpoint)
{
	uint8_t endpoint_number = usb_endpoint_number(endpoint->address);
	endpoint->device->reg->endptnaken &= ~USB0_ENDPTNAKEN_EPTNE(1 << endpoint_number);
}


/**
 * Tries to locate the descriptor for a given endpoint.
 */
usb_endpoint_descriptor_t *usb_endpoint_descriptor(usb_endpoint_t *endpoint)
{
	uintptr_t descriptor_address;
	usb_configuration_descriptor_t *configuration = endpoint->device->active_configuration;

	int32_t descriptor_remaining;

	// If we're not configured, return NULL.
	if (!configuration) {
		return NULL;
	}

	// Start off with the address of the configuration descriptor, whose subordinate
	// descriptor contains all of the endpoint descriptors.
	descriptor_address = (uintptr_t)configuration;

	// Iterate until we find a sentinel entry, which has zero length.
    descriptor_remaining = configuration->total_length;
	while (descriptor_remaining > 0) {
		usb_descriptor_t *descriptor = (usb_descriptor_t *)descriptor_address;

		bool is_endpoint = (descriptor->type == USB_DESCRIPTOR_TYPE_ENDPOINT);
		bool index_matches = (descriptor->data[0] == endpoint->address);

		// If we've found the endpoint, return it.
		if (is_endpoint && index_matches) {
			return (usb_endpoint_descriptor_t *)descriptor;
		}

		descriptor_remaining -= descriptor->length;
		descriptor_address   += descriptor->length;
	}

	return NULL;
}


/**
 *  Configures an endpoint object for use by configuring its dQH.
 */
void usb_endpoint_init(usb_endpoint_t *endpoint)
{
	usb_endpoint_flush(endpoint);

	uint16_t max_packet_size = endpoint->device->device_descriptor->ep0_max_packet_size;
	usb_transfer_type_t transfer_type = USB_TRANSFER_TYPE_CONTROL;

	// If we have an endpoint descriptor, modify the defaults to reflect the
	// data in the descriptor...
	usb_endpoint_descriptor_t *endpoint_descriptor = usb_endpoint_descriptor(endpoint);
	if (endpoint_descriptor) {
		max_packet_size = endpoint_descriptor->max_packet_size;
		transfer_type   = endpoint_descriptor->transfer_type;
	}

	// .. and use the parameters to configure the endpoint.
	usb_configure_endpoint_queue_head(endpoint, max_packet_size, transfer_type);
}


static void usb_check_for_setup_events(usb_peripheral_t* const device)
{
	const uint32_t endptsetupstat = usb_get_endpoint_setup_status(device);
	uint32_t endptsetupstat_bit = 0;

	if( endptsetupstat ) {
		for( uint_fast8_t i=0; i<6; i++ ) {
			endptsetupstat_bit = USB0_ENDPTSETUPSTAT_ENDPTSETUPSTAT(1 << i);
			if( endptsetupstat & endptsetupstat_bit ) {
				usb_endpoint_t* const endpoint = usb_endpoint_from_address(
						usb_endpoint_address(USB_TRANSFER_DIRECTION_OUT, i),
						device);

				// TODO: Clean up this duplicated effort by providing
				// a cleaner way to get the SETUP data.
				// FIXME: move setup into the device, as it's a device-context event
				usb_copy_setup(&endpoint->setup,
						   usb_queue_head(endpoint->address, endpoint->device)->setup);
				usb_copy_setup(&endpoint->in->setup,
						   usb_queue_head(endpoint->address, endpoint->device)->setup);

				// Mark the setup stage as handled, as we've grabbed its data.
				// TODO: should this be after we flush the endpoints, too?
				usb_clear_endpoint_setup_status(endptsetupstat_bit, device);

				// Ensure there are no pending control transfers.
				usb_endpoint_flush(endpoint->in);
				usb_endpoint_flush(endpoint->out);

				// If we have a setup_complete callback, call it.
				if (endpoint && endpoint->setup_complete) {
					endpoint->setup_complete(endpoint);
				}
			}
		}
	}
}


static void usb_check_for_transfer_events(usb_peripheral_t* const device)
{
	const uint32_t endptcomplete = usb_get_endpoint_complete(device);

	uint32_t endptcomplete_out_bit = 0;
	uint32_t endptcomplete_in_bit = 0;
	if( endptcomplete ) {
		for( uint_fast8_t i=0; i<6; i++ ) {

			endptcomplete_out_bit = USB0_ENDPTCOMPLETE_ERCE(1 << i);
			if( endptcomplete & endptcomplete_out_bit ) {
				usb_clear_endpoint_complete(endptcomplete_out_bit, device);
				usb_endpoint_t* const endpoint =
					usb_endpoint_from_address(
						usb_endpoint_address(USB_TRANSFER_DIRECTION_OUT, i),
						device);
				if( endpoint && endpoint->transfer_complete ) {
					endpoint->transfer_complete(endpoint);
				}
			}

			endptcomplete_in_bit = USB0_ENDPTCOMPLETE_ETCE(1 << i);
			if( endptcomplete & endptcomplete_in_bit ) {
				usb_clear_endpoint_complete(endptcomplete_in_bit, device);
				usb_endpoint_t* const endpoint =
					usb_endpoint_from_address(
						usb_endpoint_address(USB_TRANSFER_DIRECTION_IN, i),
						device);
				if( endpoint && endpoint->transfer_complete ) {
					endpoint->transfer_complete(endpoint);
				}
			}
		}
	}
}


/**
 * Handle notification that the USB host controller may have put
 * our device into suspend.
 */
static void usb_handle_suspend(usb_peripheral_t *const device)
{
	// If the device is currently suspend, handle it.
	if (device->reg->usbsts.dc_suspend) {

		// TODO: issue a callback, so the application using this
		// driver can handle going into a low-power state
	}
}


/**
 * Handle notification of a status change on the given device,
 * which may indicate connect/disconnect/resume.
 */
static void usb_handle_port_status_change(usb_peripheral_t *const device)
{
	// TODO: handle these where we can; and issue callbacks where appropriate
}



/**
 * Interrupt handler for device-mode USB interrupts.
 */
void usb_device_isr(usb_peripheral_t *const device)
{
	const usb_interrupt_flags_t status = usb_get_status(device);

	if (status.all == 0 ) {
		// Nothing to do.
		return;
	}

	if (status.usb_interrupt) {
		// USB:
		// - Completed transaction transfer descriptor has IOC set.
		// - Short packet detected.
		// - SETUP packet received.

		usb_check_for_setup_events(device);
		usb_check_for_transfer_events(device);
	}

	if (status.sof_received) {
		// Start Of Frame received.
		// TODO: allow the application to request a SOF-recieved callback,
		// which allow it to e.g. watchdog on USB communications
		// TODO: determine if we need to store a "last packet" time, which
		// may help us to detect suspend, if the hardware isn't reliably going
		// to generaete dc_suspend
	}

	if (status.port_change_detected) {
		// Port change detect:
		// Port controller entered full- or high-speed operational state.
		usb_handle_port_status_change(device);
	}

	if (status.dc_suspend) {
		// Device controller suspend.
		usb_handle_suspend(device);
	}

	// If the USB host is issuing a bus reset, handle it accordingly.
	if (status.usb_reset_received) {
		usb_bus_reset(device);
	}

	if (status.system_error) {
		// USB error:
		// Completion of a USB transaction resulted in an error condition.
		// Set along with USBINT if the TD on which the error interrupt
		// occurred also had its interrupt on complete (IOC) bit set.
		// The device controller detects resume signalling only.
	}

	if (status.nak_interrupt) {
		// Both the TX/RX endpoint NAK bit and corresponding TX/RX endpoint
		// NAK enable bit are set.
	}
}

void usb0_isr()
{
	usb_device_isr(&usb_peripherals[0]);
}


void usb1_isr()
{
	usb_device_isr(&usb_peripherals[1]);
}

