#!/usr/bin/env python3
#
# This file is part of GreatFET

from __future__ import print_function

import argparse
import errno
import sys
import time
import os
import array
import tempfile
import threading

import crcmod

# Temporary?
import usb

from zipfile import ZipFile

import greatfet

from greatfet import GreatFET, find_greatfet_asset
from greatfet.utils import GreatFETArgumentParser, log_silent, log_error

# Default sample-delivery timeout.
SAMPLE_DELIVERY_TIMEOUT_MS  = 100

# Speed constants.
SPEED_HIGH = 0
SPEED_FULL = 1
SPEED_LOW  = 2

# Speed name constants.
SPEED_NAMES = {
    SPEED_HIGH: 'high',
    SPEED_FULL: 'full',
    SPEED_LOW:  'low',
}

class USBDelineator:
    """ Class that breaks a USB data stream into its component parts. """

    # Polynomial used for the USB CRC16.
    USB_CRC_POLYNOMIAL = 0x18005

    inner_data_crc = staticmethod(crcmod.mkCrcFun(USB_CRC_POLYNOMIAL))

    @classmethod
    def data_crc(cls, data):
        return cls.inner_data_crc(data) ^ 0xFFFF


    def __init__(self):

        self.found_first_token = False

        # Create holding buffers for our "packet boundary" data and for our
        # data pending packetization.
        self.pending_data = []
        self.pending_boundary_data = bytearray()
        self.packet_boundaries = []

        # Store a count of bytes already parsed.
        self.bytes_parsed = 0




    def add_boundary(self, byte_number):
        """ Adds a given boundary to our list of USB boundaries. """

        # FIXME: handle rollover for extra-long captures
        if byte_number < self.bytes_parsed:
            return

        # If we already know about this boundary, ignore it.
        if byte_number in self.packet_boundaries:
            return

        # Otherwise, add this to our list of packet boundaries...
        self.packet_boundaries.append(byte_number)

        # ... and check to see if it helps us chunk any data.
        #self.handle_new_data()


    def add_boundary_bytes(self, data):
        """ Processes a set of raw bytes that indicate USB packet boundaries. """

        # Add in our new data...
        self.pending_boundary_data.extend(data)

        # ... and extract any boundaries we can from it.
        while len(self.pending_boundary_data) >= 4:
            next_boundary_raw = self.pending_boundary_data[0:4]
            del self.pending_boundary_data[0:4]

            next_boundary = int.from_bytes(next_boundary_raw, byteorder='little')
            self.add_boundary(next_boundary)



    def submit_data(self, data):
        """ Processes a set of USB data for delineation. """

        # Add our new data to our list of pending data...
        self.pending_data.extend(data)

        # ... and check to see if we can break it into packets.
        self.handle_new_data()


    def divine_next_boundary(self):

        TOKEN_LENGTH         = 3
        HANDSHAKE_LENGTH     = 1

        TOKEN_PID_SUFFIX     = 0b01
        HANDSHAKE_PID_SUFFIX = 0b10
        DATA_PID_SUFFIX      = 0b11

        data = self.pending_data[:]
        relative_offset = 0

        # If our byte seems likely to be a "bus turnover" byte,
        # consider the next byte, instead.
        if not (data[0] & 0xF0):
            if len(self.pending_data) > 1:
                del data[0]
                relative_offset = 1

        # Extract the last two bits of the PID, which tell us what category
        # of packet this is.
        pid_suffix = data[0] & 0b11

        if pid_suffix == TOKEN_PID_SUFFIX:
            return TOKEN_LENGTH + relative_offset

        elif pid_suffix == HANDSHAKE_PID_SUFFIX:
            return HANDSHAKE_LENGTH + relative_offset

        elif (pid_suffix == DATA_PID_SUFFIX) & (len(self.pending_data) >= 3):

            for i in range(3, 512):

                if i > len(self.pending_data):
                    break

                payload = bytes(data[1: i-2])
                payload_crc = self.data_crc(payload)

                packet_crc = data[i-2] | data[i-1] << 8

                print("Trying payload: {}{}; check = {}, looking for {}".format(len(payload) + 3, payload, payload_crc, packet_crc))

                if payload_crc == packet_crc:
                    print("FOUND!")
                    return len(data) + relative_offset


        next_packet_boundary = min(self.packet_boundaries)
        return next_packet_boundary - self.bytes_parsed


    def trim_to_first_token(self):

        while self.pending_data:
            byte = self.pending_data[0]

            if (byte & 0xf0) and ((byte & 0x11) == 0b01):
                self.found_first_token = True
                return

            del self.pending_data[0]


    def handle_new_data(self):
        """
        Checks to see if any new {data, packet boundary} information helps us generate packets, and
        generates packets if we can.
        """

        if not self.found_first_token:
            self.trim_to_first_token()

        # Repeatedly try to extract a packet until we no longer can.
        while True:

            # If we don't have both pending data and delineation information, we can't do anything. Abort.
            if (not self.packet_boundaries) or (not self.pending_data):
                return

            # FIXME: packet delineations _should_ be monotonic, so this should just be [0]?
            next_boundary_relative = self.divine_next_boundary()

            # If our next boundary occurs after our pending data, we can't do anything yet. Abort.
            if next_boundary_relative >= len(self.pending_data):
                return

            #
            # Otherwise, we have data we can process. Do so.
            #

            # Grab and extract our new packet...
            new_packet = self.pending_data[:next_boundary_relative]
            new_packet_length = len(new_packet)

            # Remove the parsed packet, and move forward our "parse progress" marker.
            del self.pending_data[0:new_packet_length]
            self.bytes_parsed += new_packet_length

            # Remove any packet boundaries that we've already parsed.
            self.packet_boundaries = [b for b in self.packet_boundaries if b > self.bytes_parsed]

            # Finally, emit the new packet.
            self.emit_packet(new_packet)


    def emit_packet(self, data):
        """ Submits a given packet to our output driver for processing. """

        # Sometimes, our sampling method captures the bus-turnaround byte before our packet.
        # If we did, strip it off before processing it.
        if data and data[0] in (0x02, 0x09, 0x0A):
            del data[0]

        if not data:
            return

        # FIXME: call a user-provided callback, or several?
        PIDS = {
            0b0001: 'OUT',
            0b1001: 'IN',
            0b0101: 'SOF',
            0b1101: 'SETUP',

            0b0011: 'DATA0',
            0b1011: 'DATA1',
            0b0111: 'DATA2',
            0b1111: 'MDATA',

            0b0010: 'ACK',
            0b1010: 'NAK',
            0b1110: 'STALL',
            0b0110: 'NYET',

            0b1100: 'PRE',
            0b0100: 'ERR',
            0b1000: 'SPLIT',
            0b0100: 'PING',
        }

        pid = data[0] & 0x0f

        # Skip SOFs.
        if pid == 0b0101:
            return

        if pid in PIDS:
            print("{} PACKET: {}".format(PIDS[pid], ["{:02x}".format(byte) for byte in data]))
        else:
            print("UNKNOWN PACKET: {}".format(["{:02x}".format(byte) for byte in data]))


class USBHackDelineator:
    """ Class that breaks a USB data stream into its component parts. """

    # Polynomial used for the USB CRC16.
    USB_CRC_POLYNOMIAL = 0x18005

    inner_data_crc = staticmethod(crcmod.mkCrcFun(USB_CRC_POLYNOMIAL))

    @classmethod
    def data_crc(cls, data):
        return cls.inner_data_crc(data) ^ 0xFFFF


    def __init__(self):

        self.found_first_token = False

        # Create holding buffers for our "packet boundary" data and for our
        # data pending packetization.
        self.pending_data = []

        # Store a count of bytes already parsed.
        self.bytes_parsed = 0


    def submit_data(self, data):
        """ Processes a set of USB data for delineation. """

        # Add our new data to our list of pending data...
        self.pending_data.extend(data)

        # ... and check to see if we can break it into packets.
        self.divine_boundaries()


    @staticmethod
    def is_valid_pid(byte):

        pid_low = byte & 0x0f
        pid_high = byte >> 4
        pid_high_inverse = pid_high ^ 0xf

        return pid_low == pid_high_inverse


    def divine_boundaries(self):

        TOKEN_LENGTH         = 3
        HANDSHAKE_LENGTH     = 1

        TOKEN_PID_SUFFIX     = 0b01
        HANDSHAKE_PID_SUFFIX = 0b10
        DATA_PID_SUFFIX      = 0b11
        SPECIAL_PID_SUFFIX   = 0b00


        while self.pending_data:

            # Grab the first byte of our data, which should be our USB packet ID.
            pid = self.pending_data[0]

            # If this packet isn't a valid PID, it doesn't start a USB packet. Skip it.
            if not self.is_valid_pid(pid):
                del self.pending_data[0]
                continue

            # Extract the last two bits of the PID, which tell us what category
            # of packet this is.
            pid_suffix = pid & 0b11

            # If this is a TOKEN pid, we always have three bytes of data.
            if pid_suffix == TOKEN_PID_SUFFIX:
                if len(self.pending_data) < TOKEN_LENGTH:
                    return

                packet = self.pending_data[0:TOKEN_LENGTH]
                del self.pending_data[0:TOKEN_LENGTH]

                self.emit_packet(packet)

            # If this is a handshake packet, we always have just the PID of data.
            elif pid_suffix == HANDSHAKE_PID_SUFFIX:
                del self.pending_data[0]
                self.emit_packet([pid])


            # If this is a handshake packet, we always have just the PID of data.
            elif pid_suffix == SPECIAL_PID_SUFFIX:
                del self.pending_data[0]
                self.emit_packet([pid])


            # If this is a DATA pid, we'll need to try various lengths to see if anything matches our framing format.
            elif (pid_suffix == DATA_PID_SUFFIX) & (len(self.pending_data) >= 3):

                # Try every currently possible packet length.
                for length in range(3, 515):

                    if length > len(self.pending_data):
                        return

                    # Extract the payload of the given packet, and compute its CRC.
                    payload = bytes(self.pending_data[1:length-2])
                    payload_crc = self.data_crc(payload)

                    # Read the end of the theoretical packet, and parse it as a CRC.
                    packet_crc = self.pending_data[length-2] | (self.pending_data[length-1] << 8)

                    # If they match, odds are this is the end of the data packet.
                    if payload_crc == packet_crc:
                        packet = self.pending_data[0:length]
                        del self.pending_data[0:length]

                        self.emit_packet(packet)


    def emit_packet(self, data):
        """ Submits a given packet to our output driver for processing. """

        # FIXME: call a user-provided callback, or several?
        PIDS = {
            0b0001: 'OUT',
            0b1001: 'IN',
            0b0101: 'SOF',
            0b1101: 'SETUP',

            0b0011: 'DATA0',
            0b1011: 'DATA1',
            0b0111: 'DATA2',
            0b1111: 'MDATA',

            0b0010: 'ACK',
            0b1010: 'NAK',
            0b1110: 'STALL',
            0b0110: 'NYET',

            0b1100: 'PRE',
            0b0100: 'ERR',
            0b1000: 'SPLIT',
            0b0100: 'PING',
        }

        pid = data[0] & 0x0f

        # Skip SOFs.
        if pid == 0b0101:
            return

        if pid in PIDS:
            print("{} PACKET: {}".format(PIDS[pid], ["{:02x}".format(byte) for byte in data]))
        else:
            print("UNKNOWN PACKET: {}".format(["{:02x}".format(byte) for byte in data]))



def read_rhododendron_m0_loadable():
    """ Read the contents of the default Rhododendron loadable from the tools distribution. """

    RHODODENDRON_M0_FILENAME = 'rhododendron_m0.bin'

    filename = os.getenv('RHODODENDRON_M0_BIN', RHODODENDRON_M0_FILENAME)

    # If we haven't found another path, fall back to an m0 binary in the current directory.
    if filename is None:
        filename = RHODODENDRON_M0_FILENAME

    with open(filename, 'rb') as f:
        return f.read()


def main():

    # Create a new delineator to chunk the received data into packets.
    delineator     = USBHackDelineator()

    # Set up our argument parser.
    parser = GreatFETArgumentParser(description="Simple Rhododendron capture utility for GreatFET.", verbose_by_default=True)
    parser.add_argument('-o', '-b', '--binary', dest='binary', metavar='<filename>', type=str,
                        help="Write the raw samples captured to a file with the provided name.")
    parser.add_argument('--m0', dest="m0", type=argparse.FileType('rb'), metavar='<filename>',
                        help="loads the specific m0 coprocessor 'loadable' instead of the default Rhododendron one")
    parser.add_argument('-F', '--full-speed', dest='speed', action='store_const', const=SPEED_FULL, default=SPEED_HIGH,
                        help="Capture full-speed data.")
    parser.add_argument('-L', '--low-speed', dest='speed', action='store_const', const=SPEED_LOW, default=SPEED_HIGH,
                        help="Capture low-speed data.")
    parser.add_argument('-H', '--high-speed', dest='speed', action='store_const', const=SPEED_HIGH,
                        help="Capture high-speed data. The default.")
    parser.add_argument('-O', '--stdout', dest='write_to_stdout', action='store_true',
                         help='Provide this option to log the received data to the stdout.. Implies -q.')


    # And grab our GreatFET.
    args = parser.parse_args()

    # If we're writing binary samples directly to stdout, don't emit logs to stdout; otherwise, honor the
    # --quiet flag.
    if args.write_to_stdout:
        log_function = log_silent
    else:
        log_function = parser.get_log_function()

    # Ensure we have at least one write operation.
    if not (args.binary or args.write_to_stdout):
        parser.print_help()
        sys.exit(-1)

    # Find our GreatFET.
    device = parser.find_specified_device()

    # Load the Rhododendron firmware loadable into memory.
    try:
        if args.m0:
            data = args.m0.read()
        else:
            data = read_rhododendron_m0_loadable()
    except (OSError, TypeError):
        log_error("Can't find a Rhododendron m0 program to load!")
        log_error("We can't run without one.")
        sys.exit(-1)


    # Bring our Rhododendron board online; and capture communication parameters.
    buffer_size, endpoint = device.apis.usb_analyzer.initialize(args.speed, timeout=10000, comms_timeout=10000)

    # Debug only: setup a pin to track when we're handling SGPIO data.
    debug_pin = device.gpio.get_pin('J1_P3')
    debug_pin.set_direction(debug_pin.DIRECTION_OUT)

    # Start the m0 loadable for Rhododendron.
    device.m0.run_loadable(data)

    # Print what we're doing and our status.
    log_function("Reading raw {}-speed USB data!\n".format(SPEED_NAMES[args.speed]))
    log_function("Press Ctrl+C to stop reading data from device.")

    # Now that we're done with all of that setup, perform our actual sampling, in a tight loop,
    device.apis.usb_analyzer.start_capture()
    transfer_buffer = array.array('B', b"\0" * buffer_size)

    # FIXME: abstract
    delineation_buffer = array.array('B', b"\0" * 512)

    total_captured = 0

    try:
        log_function("Captured 0 bytes.", end="\r")

        while True:

            #try:
            #    new_delineation_bytes = device.comms.device.read(0x83, delineation_buffer, SAMPLE_DELIVERY_TIMEOUT_MS)
            #    delineator.add_boundary_bytes(delineation_buffer[0:new_delineation_bytes])

            #except usb.core.USBError as e:
            #    if e.errno != errno.ETIMEDOUT:
            #        raise

            # Capture data from the device, and unpack it.
            try:
                new_samples = device.comms.device.read(endpoint, transfer_buffer, SAMPLE_DELIVERY_TIMEOUT_MS)
                samples =transfer_buffer[0:new_samples - 1]

                total_captured += new_samples
                log_function("Captured {} bytes.".format(total_captured), end="\r")

                delineator.submit_data(samples)


            except usb.core.USBError as e:
                if e.errno != errno.ETIMEDOUT:
                    raise

    except KeyboardInterrupt:
        pass
    except usb.core.USBError as e:
        log_error("")
        if e.errno == 32:
            log_error("ERROR: Couldn't pull data from the device fast enough! Aborting.")
        else:
            log_error("ERROR: Communications failure -- check the connection to -- and state of  -- the GreatFET. ")
            log_error("(More debug information may be available if you run 'gf dmesg').")
            log_error(e)
    finally:

        # No matter what, once we're done stop the device from sampling.
        device.apis.usb_analyzer.stop_capture()


if __name__ == '__main__':
    main()
