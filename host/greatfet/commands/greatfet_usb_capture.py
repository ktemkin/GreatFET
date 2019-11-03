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

    def __init__(self):

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
        self.handle_new_data()

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


    def handle_new_data(self):
        """
        Checks to see if any new {data, packet boundary} information helps us generate packets, and
        generates packets if we can.
        """

        # Repeatedly try to extract a packet until we no longer can.
        while True:

            # If we don't have both pending data and delineation information, we can't do anything. Abort.
            if (not self.packet_boundaries) or (not self.pending_data):
                return

            # FIXME: packet delineations _should_ be monotonic, so this should just be [0]?
            next_packet_boundary = min(self.packet_boundaries)
            next_boundary_relative = next_packet_boundary - self.bytes_parsed

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

            # Remove the active packet boundary, as we've already consumed all data up to it.
            self.packet_boundaries.remove(next_packet_boundary)

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
        if pid in PIDS:
            print("{} PACKET: {}".format(PIDS[pid], ["{:02x}".format(byte) for byte in data]))
        else:
            print("UNKNOWN PACKET: {}".format(["{:02x}".format(byte) for byte in data]))


class RhododenronPacketParser:
    """ Parses a rhododendron data-stream, and chunks it into Rhododendron packets. """

    # Rhododendron packet types.
    PACKET_TYPE_USB_DATA     = 0
    PACKET_TYPE_DELINEATION  = 1

    PACKET_PAYLOAD_SIZES = {
        PACKET_TYPE_USB_DATA:    32,
        PACKET_TYPE_DELINEATION: 12,
    }


    def __init__(self, delineator):
        """ Creates a new Rhododendron Packet Parser (TM).

        Parameters:
            delineator -- A USBDelinator object that will accept data extracted from Rhododendron packets,
                          and break it into USB packets.
        """

        self.delineator = delineator

        # Start off assuming we're not currently parsing any data.
        self.current_packet_type   = None
        self.current_packet_data   = []
        self.current_packet_length = None

        # Statistics.
        self.bytes_processed = 0


    def submit_data(self, data):
        """ Handles new Rhododendron packet data. """

        # Parse the data we have until there's no data left.
        while data:

            # If we don't currently have a packet, use the first byte of the data to start a new one.
            if self.current_packet_type is None:
                self.start_new_packet(data[0])
                del data[0]

            # Determine how much more data we're looking for in the given Rhododendron packet.
            data_remaining = self.current_packet_length - len(self.current_packet_data)

            # Extract up to the remaining packet length from our incoming data stream...
            new_data = data[0:data_remaining]
            del data[0:data_remaining]

            # ... and add it to our packet-in-progress.
            self.current_packet_data.extend(new_data)

            # If we've just finished a packet, handle it!
            if len(self.current_packet_data) == self.current_packet_length:
                self.handle_packet(self.current_packet_type, self.current_packet_data)
                self.current_packet_type = None
                self.current_packet_data = []


    def start_new_packet(self, packet_id):
        """ Configures the parser to accept a new packet, based on its first-byte ID. """

        # If we don't know the size of the given packet type, fail out.
        if packet_id not in self.PACKET_PAYLOAD_SIZES:
            raise ValueError("unknown packet type {}; failing out after {} bytes!".format(packet_id, self.bytes_processed))

        # Store the current packet's type and size.
        self.current_packet_type = packet_id
        self.current_packet_length = self.PACKET_PAYLOAD_SIZES[packet_id]

        self.bytes_processed += 1


    def handle_packet(self, packet_type, packet_data):
        """ Handles a complete Rhododendron packet. """

        self.bytes_processed += len(packet_data)

        # If this is an "end event" packet, grab the point at which
        # we're supposed to emit the packet.
        if packet_type == self.PACKET_TYPE_DELINEATION:
            while packet_data:

                # Grab a single termination point from the array...
                termination = int.from_bytes(packet_data[0:1], byteorder='little')
                del packet_data[0:2]

                # ... and consider it a new USB packet boundary.
                self.delineator.add_boundary(termination)


        # If this is a USB data packet, submit it directly to the delineator.
        elif packet_type == self.PACKET_TYPE_USB_DATA:
            self.delineator.submit_data(packet_data)



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
    delineator     = USBDelineator()

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

            try:
                new_delineation_bytes = device.comms.device.read(0x83, delineation_buffer, SAMPLE_DELIVERY_TIMEOUT_MS)
                delineator.add_boundary_bytes(delineation_buffer[0:new_delineation_bytes])

            except usb.core.USBError as e:
                if e.errno != errno.ETIMEDOUT:
                    raise

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
