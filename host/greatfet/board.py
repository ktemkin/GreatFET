#
# This file is part of GreatFET
#

"""
Module containing the core definitions for a GreatFET board.
"""

import usb
import time

from .protocol import vendor_requests
from .errors import DeviceNotFoundError
from .peripherals.led import LED
from .peripherals.gpio import GPIO

# Default device identifiers.
GREATFET_VENDOR_ID = 0x1d50
GREATFET_PRODUCT_ID = 0x60e6

# Quirk constant that helps us identify libusb's pipe errors, which bubble
# up as generic USBErrors with errno 32 on affected platforms.
LIBUSB_PIPE_ERROR = 32

# Total seconds we should wait after a reset before reconnecting.
RECONNECT_DELAY = 3


class GreatFETBoard(object):
    """
    Class representing a USB-connected GreatFET device.
    """

    """
    The GreatFET board IDs handled by this class. Used by the default
    implementation of accepts_connected_device() to determine if a given subclass
    handles the given board ID.
    """
    HANDLED_BOARD_IDS = []

    """
    The display name of the given GreatFET board. Subclasses should override
    this with a more appropriate name.
    """
    BOARD_NAME = "Unknown GreatFET"


    """
    The mappings from GPIO names to port numbers. Paths in names can be delineated
    with underscores to group gpios. For example, if Jumper 7, Pin 3 is Port 5, Pin 11,
    you could add an entry that reads "J7_P3": (5, 11).
    """
    GPIO_MAPPINGS = {}


    @classmethod
    def autodetect(cls, **device_identifiers):
        """
        Attempts to create a new instance of the GreatFETBoard subclass
        most applicable to the given device. For example, if the attached
        board is a GreatFET One, this will automatically create a
        GreatFET One object.

        Accepts the same arguments as pyusb's usb.find() method, allowing narrowing
        to a more specific GreatFET by e.g. serial number.

        Throws a DeviceNotFoundError if no device is avaiable.
        """

        # Iterate over each subclass of GreatFETBoard until we find a board
        # that accepts the given board ID.
        for subclass in cls.__subclasses__():
            if subclass.accepts_connected_device(**device_identifiers):
                return subclass(**device_identifiers)

        # If we couldn't find a board, raise an error.
        raise DeviceNotFoundError()


    @classmethod
    def autodetect_all(cls, **device_identifiers):
        """
        Attempts to create a new instance of the GreatFETBoard subclass
        most applicable for each board present on the system-- similar to the
        behavior of autodetect.

        Accepts the same arguments as pyusb's usb.find() method, allowing narrowing
        to a more specific GreatFET by e.g. serial number.

        Returns a list of GreatFET devices, which may be empty if none are found.
        """

        devices = []

        # Iterate over each subclass of GreatFETBoard until we find a board
        # that accepts the given board ID.
        for subclass in cls.__subclasses__():

            # Get objects for all devices accepted by the given subclass.
            subclass_devices = subclass.all_accepted_devices(**device_identifiers)

            # FIXME: It's possible that two classes may choose to both advertise support
            # for the same device, in which case we'd wind up with duplicats here. We could
            # try to filter out duplicates using e.g. USB bus/device, but that assumes
            # things are USB connected.
            devices.extend(subclass_devices)

        # Return the list of all subclasses.
        return devices


    @classmethod
    def all_accepted_devices(cls, **device_identifiers):
        """
        Returns a list of all devices supported by the given class. This should be
        overridden if the device connects via anything other that USB.

        Accepts the same arguments as pyusb's usb.find() method, allowing narrowing
        to a more specific GreatFET by e.g. serial number.
        """

        devices = []

        # Grab the list of all devices that we theoretically could use.
        identifiers = cls.populate_default_identifiers(device_identifiers, find_all=True)
        raw_devices = usb.core.find(**identifiers)

        # Iterate over all of the connected devices, and filter out the devices
        # that this class doesn't connect.
        for raw_device in raw_devices:

            # We need to be specific about which device in particular we're
            # grabbing when we query things-- or we'll get the first acceptable
            # device every time. The trick here is to populate enough information
            # into the identifier to uniquely identify the device. The address
            # should do, as pyusb is only touching enmerated devices.
            identifiers['address'] = raw_device.address
            identifiers['find_all'] = False

            # If we support the relevant device _instance_, and it to our list.
            if cls.accepts_connected_device(**identifiers):
                devices.append(cls(**identifiers))

        return devices



    @classmethod
    def accepts_connected_device(cls, **device_identifiers):
        """
        Returns true iff the provided class is appropriate for handling a connected
        GreatFET.

        Accepts the same arguments as pyusb's usb.find() method, allowing narrowing
        to a more specific GreatFET by e.g. serial number.
        """
        try:
            potential_device = cls(**device_identifiers)
        except DeviceNotFoundError:
            return False
        except usb.core.USBError as e:

            # A pipe error here likely means the device didn't support a start-up
            # command, and STALLED.
            # We'll interpret that as a "we don't accept this device" by default.
            if e.errno == LIBUSB_PIPE_ERROR:
                return False
            else:
                raise e

        try:
            board_id = potential_device.board_id()
        finally:
            potential_device.close()

        # Accept only GreatFET devices whose board IDs are handled by this
        # class. This is mostly used by subclasses, which should override
        # HANDLED_BOARD_IDS.
        return board_id in cls.HANDLED_BOARD_IDS


    @staticmethod
    def populate_default_identifiers(device_identifiers, find_all=False):
        """
        Populate a dictionary of default identifiers-- which can
        be overridden or extended by arguments to the function.

        device_identifiers -- any user-specified identifers; will override
            the default identifiers in the event of a conflit
        """

        # By default, accept any device with the default vendor/product IDs.
        identifiers = {
            'idVendor': GREATFET_VENDOR_ID,
            'idProduct': GREATFET_PRODUCT_ID,
            'find_all': find_all,
        }
        identifiers.update(device_identifiers)

        return identifiers



    def __init__(self, **device_identifiers):
        """
        Instantiates a new connection to a GreatFET device; by default connects
        to the first available GreatFET.

        Accepts the same arguments as pyusb's usb.find() method, allowing narrowing
        to a more specific GreatFET by serial number.
        """

        # By default, accept any device with the default vendor/product IDs.
        self.identifiers = self.populate_default_identifiers(device_identifiers)

        # For convenience, allow serial_number=None to be equivalent to not
        # providing a serial number: a  GreatFET with any serail number will be
        # accepted.
        if 'serial_number' in self.identifiers and self.identifiers['serial_number'] is None:
            del self.identifiers['serial_number']

        # Connect to the first available GreatFET device.
        try:
            self.device = usb.core.find(**self.identifiers)
        except usb.core.USBError as e:
            # On some platforms, providing identifiers that don't match with any
            # real device produces a USBError/Pipe Error. We'll convert it into a
            # DeviceNotFoundError.
            if e.errno == LIBUSB_PIPE_ERROR:
                raise DeviceNotFoundError()
            else:
                raise e

        # If we couldn't find a GreatFET, bail out early.
        if self.device is None:
            raise DeviceNotFoundError()

        # Ensure that we have an active USB connection to the device.
        self._initialize_usb()

        # Final sanity check: if we don't handle this board ID, bail out!
        if self.HANDLED_BOARD_IDS and (self.board_id() not in self.HANDLED_BOARD_IDS):
            raise DeviceNotFoundError()


    def _initialize_usb(self):
        """Sets up our USB connection to the GreatFET device."""

        # For now, the GreatFET is only providing a single configuration, so we
        # can accept the first configuration provided.
        self.device.set_configuration()


    def board_id(self):
        """Reads the board ID number for the GreatFET device."""

        # Query the board for its ID number.
        response = self.vendor_request_in(vendor_requests.READ_BOARD_ID, length=1)
        return response[0]


    def board_name(self):
        """Returns the human-readable product-name for the GreatFET device."""
        return self.BOARD_NAME


    def firmware_version(self):
        """Reads the board's firmware version."""

        # Query the board for its firmware version, and convert that to a string.
        return self.vendor_request_in_string(vendor_requests.READ_VERSION_STRING, length=255)


    def serial_number(self, as_hex_string=True):
        """Reads the board's unique serial number."""
        result = self.vendor_request_in(vendor_requests.READ_PARTID_SERIALNO, length=24)

        # The serial number starts eight bytes in.
        result = result[8:]

        # If we've been asked to convert this to a hex string, do so.
        if as_hex_string:
            result = _to_hex_string(result)

        return result


    def usb_serial_number(self):
        """ Reports the device's USB serial number. """
        return self.device.serial_number


    def part_id(self, as_hex_string=True):
        """Reads the board's unique serial number."""
        result = self.vendor_request_in(vendor_requests.READ_PARTID_SERIALNO, length=24)

        # The part ID constitues the first eight bytes of the response.
        result = result[0:7]
        if as_hex_string:
            result = _to_hex_string(result)

        return result


    def reset(self, reconnect=True, switch_to_external_clock=False):
        """
        Reset the GreatFET device.

        Arguments:
            reconect -- If True, this method will wait for the device to
                finish the reset and then attempt to reconnect.
            switch_to_external_clock -- If true, the device will accept a 12MHz
                clock signal on P4_7 (J2_P11 on the GreatFET one) after the reset.
        """

        type = 1 if switch_to_external_clock else 0

        try:
            self.vendor_request_out(vendor_requests.RESET, value=type)
        except usb.core.USBError as e:
            pass

        # If we're to attempt a reconnect, do so.
        connected = False
        if reconnect:
            time.sleep(RECONNECT_DELAY)
            self.__init__(**self.identifiers)

            # FIXME: issue a reset to all device peripherals with state, here?


    def switch_to_external_clock(self):
        """
        Resets the GreatFET, and starts it up again using an external clock 
        source, rather than the onboard crystal oscillator.
        """
        self.reset(switch_to_external_clock=True)



    def close(self):
        """
        Dispose pyUSB resources allocated by this connection.  This connection
        will no longer be usable.
        """
        usb.util.dispose_resources(self.device)


    def _vendor_request(self, direction, request, length_or_data=0, value=0, index=0, timeout=1000):
        """Performs a USB vendor-specific control request.

        See also _vendor_request_in()/_vendor_request_out(), which provide a
        simpler syntax for simple requests.

        Args:
            request -- The number of the vendor request to be performed. Usually
                a constant from the protocol.vendor_requests module.
            value -- The value to be passed to the vendor request.

        For IN requests:
            length_or_data -- The length of the data expected in response from the request.
        For OUT requests:
            length_or_data -- The data to be sent to the device.
        """
        return self.device.ctrl_transfer(
            direction | usb.TYPE_VENDOR | usb.RECIP_DEVICE,
            request, value, index, length_or_data, timeout)


    def vendor_request_in(self, request, length, value=0, index=0, timeout=1000):
        """Performs a USB control request that expects a respnose from the GreatFET.

        Args:
            request -- The number of the vendor request to be performed. Usually
                a constant from the protocol.vendor_requests module.
            length -- The length of the data expected in response from the request.
        """
        return self._vendor_request(usb.ENDPOINT_IN, request, length,
            value=value, index=index, timeout=timeout)


    def vendor_request_in_string(self, request, length=255, value=0, index=0, timeout=1000,
            encoding='utf-8'):
        """Performs a USB control request that expects a respnose from the GreatFET.

        Interprets the result as an encoded string.

        Args:
            request -- The number of the vendor request to be performed. Usually
                a constant from the protocol.vendor_requests module.
            length -- The length of the data expected in response from the request.
        """
        raw = self._vendor_request(usb.ENDPOINT_IN, request, length_or_data=length,
            value=value, index=index, timeout=timeout)
        return raw.tostring().decode(encoding)


    def vendor_request_out(self, request, value=0, index=0, data=None, timeout=1000):
        """Performs a USB control request that provides data to the GreatFET.

        Args:
            request -- The number of the vendor request to be performed. Usually
                a constant from the protocol.vendor_requests module.
            value -- The value to be passed to the vendor request.
        """
        return self._vendor_request(usb.ENDPOINT_OUT, request, value=value,
            index=index, length_or_data=data, timeout=timeout)


    @staticmethod
    def _build_command_prelude(class_number, verb):
        """Builds a libgreat command prelude, which identifies the command
        being executed to libgreat.
        """
        import struct
        return struct.pack("<II", class_number, verb)


    def execute_command(self, class_number, verb, data=None, timeout=1000,
            encoding=None, max_response_length=4096):
        """Executes a GreatFET command.

        Args:
            class_number -- The class number for the given command.
                See the GreatFET wiki for a list of class numbers.
            verb -- The verb number for the given command.
                See the GreatFET wiki for the given class.
            data -- Data to be transmitted to the GreatFET.
            timeout -- Maximum command execution time, in ms.
            encoding -- If specified, the response data will attempt to be
                decoded in the provided format.
            max_response_length -- If less than 4096, this parameter will
                cut off the provided response at the given length.

        Returns any data recieved in response.
        """

        # FIXME: these should be moved to a backend module in the libgreat
        # host library
        LIBGREAT_REQUEST_NUMBER = 0x65
        LIBGREAT_MAX_COMMAND_SIZE = 4096

        # Build the command header, which identifies the command to be executed.
        prelude = self._build_command_prelude(class_number, verb)

        # If we have data, build it into our request.
        if data:
            to_send = prelude + data

            if len(to_send) > LIBGREAT_MAX_COMMAND_SIZE:
                raise ArgumentError("Command payload is too long!")

        # Otherwise, just send the prelude.
        else:
            to_send = prelude

        # Send the command (including prelude) to the device...
        # TODO: upgrade this to be able to not block?
        self.device.ctrl_transfer(
            usb.ENDPOINT_OUT | usb.TYPE_VENDOR | usb.RECIP_ENDPOINT,
            LIBGREAT_REQUEST_NUMBER, 0, 0, to_send, timeout)

        # Truncate our maximum, if necessary.
        if max_response_length > 4096:
            max_response_length = LIBGREAT_MAX_COMMAND_SIZE

        # ... and read any response the device has prepared for us.
        # TODO: use our own timeout, rather than the command timeout, to
        # avoid doubling the overall timeout
        response = self.device.ctrl_transfer(
            usb.ENDPOINT_IN | usb.TYPE_VENDOR | usb.RECIP_ENDPOINT,
            LIBGREAT_REQUEST_NUMBER, 0, 0, max_response_length, timeout)

        # If we were passed an encoding, attempt to decode the response data.
        if encoding and response:
            response = response.tostring().decode(encoding)

        # Return the device's response.
        return response


    def read_debug_ring(self, max_length=2048, clear=False, encoding='latin1'):
        """ Requests the GreatFET's debug ring.

        Args:
            max_length -- The maximum length to respond with. Must be less than 65536.
            clear -- True iff the dmesg buffer should be cleared after the request.
        """

        CLASS_DEBUG = 0x1234
        CLASS_DEBUG_VERB_READ = 0
        CLASS_DEBUG_VERB_CLEAR = 1

        response = self.execute_command(0x1234, 0, encoding='latin1')

        if clear:
            self.execute_command(0x1234, 1)

        return response


    def _populate_leds(self, led_count):
        """Adds the standard set of LEDs to the board object.

        Args:
            led_count -- The number of LEDS present on the board.
        """
        self.leds = {}
        for i in range(1, led_count + 1):
            self.leds[i] = LED(self, i)


    def _populate_gpio(self):
        """Adds GPIO pin definitions to the board's main GPIO object."""

        self.gpio = GPIO(self)

        # Handle each GPIO mapping.
        for name, pin in self.GPIO_MAPPINGS.items():
            self.gpio.register_gpio(name, pin)


def _to_hex_string(byte_array):
    """Convert a byte array to a hex string."""
    hex_generator = ('{:02x}'.format(x) for x in byte_array)
    return ''.join(hex_generator)
