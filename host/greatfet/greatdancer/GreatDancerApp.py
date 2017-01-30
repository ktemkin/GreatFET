# MAXUSBApp.py
#
# Contains class definition for MAXUSBApp.

import sys
import time
import codecs
import traceback

from .. import errors
from ..protocol import vendor_requests
from ..sensor import GreatFETSensor
from ..peripherals.i2c_device import I2CDevice

from ..greatdancer.Facedancer import *
from ..greatdancer.USB import *
from ..greatdancer.USBDevice import USBDeviceRequest
from ..greatdancer.USBEndpoint import USBEndpoint


class GreatDancerApp(FacedancerApp):
    app_name = "GreatDancer"
    app_num = 0x40 #meaningless

    # Interrupt register bits masks.
    USBSTS_D_UI  = (1 << 0)
    USBSTS_D_URI = (1 << 6)

    # Number of supported USB endpoints.
    # TODO: bump this up when we develop support using USB0 (cables flipped)
    SUPPORTED_ENDPOINTS = 4

    # USB directions
    HOST_TO_DEVICE = 0
    DEVICE_TO_HOST = 1

    def __init__(self, device, verbose=0):
        FacedancerApp.__init__(self, device, verbose)
        self.connected_device = None

        # Initialize a dictionary that will store the last setup
        # whether each endpoint is currently stalled.
        self.endpoint_stalled = {}
        for i in range(self.SUPPORTED_ENDPOINTS):
            self.endpoint_stalled[i] = False

        # Store a reference to the device's active configuration,
        # which we'll use to know which endpoints we'll need to check
        # for data transfer readiness.
        self.configuration = None

    def init_commands(self):
        pass

    def get_version(self):
        raise NotImplementedError()

    def ack_status_stage(self, direction=HOST_TO_DEVICE, endpoint_number=0):
        """
            Handles the status stage of a correctly completed transaction, by priming
            the appropriate endpint to handle the status transaction.
        """

        if direction == self.HOST_TO_DEVICE:
            # If this was an OUT request, we'll prime the output buffer to
            # respond with the ZLP expected during the status stage.
            self.send_on_endpoint(endpoint_number, data=[])
        else:
            # If this was an IN request, we'll need to set up a transfer descriptor
            # so the status phase can operate correctly. This effectively reads the
            # zero length packet from the STATUS phase.
            self.read_from_endpoint(endpoint_number)

    @staticmethod
    def _generate_endpoint_config_triplet(endpoint):

        # Figure out the endpoint's address.
        direction_mask = 0x80 if endpoint.direction == USBEndpoint.direction_in else 0x00
        address = endpoint.number | direction_mask

        # Figure out the two bytes of the maximum packet size.
        max_packet_size_h = endpoint.max_packet_size >> 8
        max_packet_size_l = endpoint.max_packet_size & 0xFF;

        # Generate the relevant packet.
        return [address, max_packet_size_l, max_packet_size_h, endpoint.transfer_type]


    def _generate_endpoint_config_command(self, config, interface_num=0):
        command = []

        for interface in config.interfaces:
            for endpoint in interface.endpoints:

                if self.verbose > 0:
                    print ("Setting up endpoint {} (direction={}, transfer_type={})".format(endpoint.number, endpoint.direction, endpoint.transfer_type))

                command.extend(self._generate_endpoint_config_triplet(endpoint))

        return command

    def connect(self, usb_device):
        self.device.vendor_request_out(vendor_requests.GREATDANCER_CONNECT)
        self.connected_device = usb_device

        if self.verbose > 0:
            print(self.app_name, "connected device", self.connected_device.name)


    def disconnect(self):
        self.device.vendor_request_out(vendor_requests.GREATDANCER_DISCONNECT)

    def send_on_endpoint(self, ep_num, data):
        if self.verbose > 3:
            print("sending on {}: {}".format(ep_num, data))

        self.device.vendor_request_out(vendor_requests.GREATDANCER_SEND_ON_ENDPOINT, index=ep_num, data=data)

    def read_from_endpoint(self, ep_num):

        # Start a nonblocking read from the given endpoint...
        self._prime_out_endpoint(ep_num)

        # ... and wait for the transfer to complete.
        while not self._transfer_is_complete(ep_num, self.HOST_TO_DEVICE):
            pass

        # Finally, return the result.
        return self._finish_primed_read_on_endpoint(ep_num)

    def stall_endpoint(self, ep_num):
        self.endpoint_stalled[ep_num] = True
        self.device.vendor_request_out(vendor_requests.GREATDANCER_STALL_ENDPOINT, index=ep_num)

    def stall_ep0(self):
        self.stall_endpoint(0)

    def set_address(self, address):
        self.device.vendor_request_out(vendor_requests.GREATDANCER_SET_ADDRESS, value=address)

    @staticmethod
    def _decode_usb_register(transfer_result):
        """
            Decodes a raw 32-bit register value from a form encoded
            for transit as a USB control request.

            transfer_result: The value returned by the vendor request.
            returns: The raw integer value of the given register.
        """
        status_hex = codecs.encode(transfer_result[::-1], 'hex')
        return int(status_hex, 16)

    def _fetch_irq_status(self):
        """
        Fetch the USB controller's pending-IRQ bitmask, which indicates
        which interrupts need to be serviced.

        returns: A raw integer bitmap.
        """
        raw_status = self.device.vendor_request_in(vendor_requests.GREATDANCER_GET_STATUS, length=4)
        return self._decode_usb_register(raw_status)

    def _fetch_setup_status(self):
        raw_status = self.device.vendor_request_in(vendor_requests.GREATDANCER_GET_SETUP_STATUS, length=4)
        return self._decode_usb_register(raw_status)


    def _handle_setup_events(self):
        """
        Handles any outstanding setup events on the USB controller.
        """

        # Determine if we have setup packets on any of our endpoints.
        status = self._fetch_setup_status()

        # If we don't, abort.
        if not status:
            return

        # Otherwise, figure out which endpoints have outstanding setup events,
        # and handle them.
        for i in range(self.SUPPORTED_ENDPOINTS):
            if status & (1 << i):
                self._handle_setup_event_on_endpoint(i)


    def _handle_setup_event_on_endpoint(self, endpoint_number):
        """
        Handles a known outstanding setup event on a given endpoint.

        endpoint_number: The endpoint number for which a setup event should be serviced.
        """

        # HACK: to maintain API compatibility with the existing facedancer API,
        # we need to know if a stall happens at any point during our handler.
        self.endpoint_stalled[endpoint_number] = False

        # Read the data from the SETUP stage...
        data = self.device.vendor_request_in(vendor_requests.GREATDANCER_READ_SETUP, length=8, index=endpoint_number)
        request = USBDeviceRequest(data)

        # If this is an OUT request, handle the data stage,
        # and add it to the request.
        is_out   = request.get_direction() == self.HOST_TO_DEVICE
        has_data = (request.length > 0)
        if is_out and has_data:
            #probably wrong, probably need to do this after transfer is complete
            # XXX FIXME XXX FIXME FIXME
            print("Reading in from endpoint!")
            new_data = self.read_from_endpoint(endpoint_number)
            data.extend(new_data)

        request = USBDeviceRequest(data)
        self.connected_device.handle_request(request)

        if not is_out and not self.endpoint_stalled[endpoint_number]:
            self.ack_status_stage(direction=self.DEVICE_TO_HOST)


    def _fetch_transfer_status(self):
        raw_status = self.device.vendor_request_in(vendor_requests.GREATDANCER_GET_TRANSFER_STATUS, length=4)
        return self._decode_usb_register(raw_status)

    def _transfer_is_complete(self, endpoint_number, direction):
        status = self._fetch_transfer_status()

        out_is_ready = (status & (1 << endpoint_number))
        in_is_ready  = (status & (1 << (endpoint_number + 16)))

        if direction == self.HOST_TO_DEVICE:
            return out_is_ready
        else:
            return in_is_ready


    def _handle_transfer_events(self):
        """
        Handles any outstanding setup events on the USB controller.
        """

        # Determine if we have setup packets on any of our endpoints.
        status = self._fetch_transfer_status()

        # If we don't, abort.
        if not status:
            return

        if self.verbose > 5:
            print("Out status: {}".format(bin(status & 0x0F)))
            print("IN status: {}".format(bin(status >> 16)))


        # XXX use _transfer_is_complete below

        # XXX
        # Otherwise, figure out which endpoints have outstanding setup events,
        # and handle them.
        for i in range(self.SUPPORTED_ENDPOINTS):
            if status & (1 << i):
                self._clean_up_transfers_for_endpoint(i, self.HOST_TO_DEVICE)

            if status & (1 << (i + 16)):
                self._clean_up_transfers_for_endpoint(i, self.DEVICE_TO_HOST)



        # Otherwise, figure out which endpoints have outstanding setup events,
        # and handle them.
        for i in range(self.SUPPORTED_ENDPOINTS):
            if status & (1 << i):
                self._handle_transfer_complete_on_endpoint(i, self.HOST_TO_DEVICE)

            if status & (1 << (i + 16)):
                self._handle_transfer_complete_on_endpoint(i, self.DEVICE_TO_HOST)


        # If we've just completed a transfer, check to see if we're now ready
        # for more data.
        self._handle_transfer_readiness()


    def _finish_primed_read_on_endpoint(self, endpoint_number):

        # Figure out how much data we'll need to read from the endpoint...
        raw_length = self.device.vendor_request_in(vendor_requests.GREATDANCER_GET_NONBLOCKING_LENGTH, index=endpoint_number, length=4)
        length = self._decode_usb_register(raw_length)

        if length == 0:
            return b''

        # ... and then read it.
        data = self.device.vendor_request_in(vendor_requests.GREATDANCER_FINISH_NONBLOCKING_READ, index=endpoint_number, length=length)
        return data.tostring()


    def _clean_up_transfers_for_endpoint(self, endpoint_number, direction):

        if self.verbose > 5:
            print("Cleaning up transfers on {}".format(endpoint_number))

        # Ask the device to clean up any transaction descriptors related to the transfer.
        self.device.vendor_request_out(vendor_requests.GREATDANCER_CLEAN_UP_TRANSFER, index=endpoint_number, value=direction)


    def _handle_transfer_complete_on_endpoint(self, endpoint_number, direction):
        """
        Handles a known outstanding setup event on a given endpoint.

        endpoint_number: The endpoint number for which a setup event should be serviced.
        """

        # If a transfer has just completed on an OUT endpoint, we've just received data!
        if direction == self.HOST_TO_DEVICE:

            # TODO: support control endpoints other than zero
            if (endpoint_number == 0):
                print("FIXME: rx'd data on the control endpoint, need to process?")
            else:
                data = self._finish_primed_read_on_endpoint(endpoint_number)
                self.connected_device.handle_data_available(endpoint_number, data)



    def _fetch_transfer_readiness(self):
        raw_status = self.device.vendor_request_in(vendor_requests.GREATDANCER_GET_TRANSFER_READINESS, length=4)
        return self._decode_usb_register(raw_status)

    def _prime_out_endpoint(self, endpoint_number):
        """
        Primes an out endpoint, allowing it to recieve data the next time the host chooses to send it.

        endpoint_number: The endpoint that should be primed.
        """
        self.device.vendor_request_out(vendor_requests.GREATDANCER_START_NONBLOCKING_READ, index=endpoint_number)


    def _handle_transfer_readiness(self):
        """
        Check to see if any non-control IN endpoints are ready to 
        accept data from our device, and handle if they are.
        """

        # If we haven't been configured yet, we can't have any
        # endpoints other than the control endpoint, and we don't n
        if not self.configuration:
            return

        # Fetch the endpoint status.
        status = self._fetch_transfer_readiness()

        # Check the status of every endpoint /except/ endpoint zero,
        # which is always a control endpoint and set handled by our
        # control transfer handler.
        for interface in self.configuration.interfaces:
            for endpoint in interface.endpoints:

                # TODO: replace the below with _is_ready_for_transfer

                # We're only interested in endpoints we can currently prime.
                # availabile. We've set each buffer up so they only accept a
                # single transfer descriptor; and thus we "buffer available"
                # is equivalent to "no current transfer primed.", which is
                # bit (endpoint_number + 16) in our status flag.
                is_in         = (endpoint.direction == USBEndpoint.direction_in)
                is_out        = not is_in
                ready_for_in  = (not status & (1 << (endpoint.number + 16)))
                ready_for_out = (not status & (1 << (endpoint.number)))

                # If this is an IN endpoint, we're ready to accept data to be
                # presented on the next IN token.
                if is_in and ready_for_in:
                    self.connected_device.handle_buffer_available(endpoint.number)

                # If this is an OUT endpoint, we'll need to prime the endpoint to
                # accept new data. This provides a place for data to go once the
                # host sends an OUT token.
                if is_out and ready_for_out:
                    self._prime_out_endpoint(endpoint.number)


    def _is_ready_for_transfer(self, ep_num, direction):

        # Fetch the endpoint status.
        status = self._fetch_transfer_readiness()

        ready_for_in  = (not status & (1 << (ep_num + 16)))
        ready_for_out = (not status & (1 << (ep_num)))

        if direction == self.HOST_TO_DEVICE:
            return ready_for_out
        else:
            return ready_for_in



    def _bus_reset(self):
        self.device.vendor_request_out(vendor_requests.GREATDANCER_BUS_RESET)


    def _configure_endpoints(self, configuration):
        endpoint_config_command = self._generate_endpoint_config_command(configuration)

        if endpoint_config_command:
            self.device.vendor_request_out(vendor_requests.GREATDANCER_SET_UP_ENDPOINTS, data=endpoint_config_command)

    def configured(self, configuration):
        self._configure_endpoints(configuration)


        self.configuration = configuration
        self._handle_transfer_readiness()


    def service_irqs(self):

        while True:
            status = self._fetch_irq_status()

            # Other bits that may be of interest:
            # D_SRI = start of frame received
            # D_PCI = port change detect (switched between low, full, high speed state)
            # D_SLI = device controller suspend
            # D_UEI = USB error; completion of transaction caused error, see usb1_isr in firmware
            # D_NAKI = both the tx/rx NAK bit and corresponding endpoint NAK enable are set

            if status & self.USBSTS_D_UI:
                self._handle_setup_events()
                self._handle_transfer_events()

            if status & self.USBSTS_D_URI:
                self._bus_reset()

