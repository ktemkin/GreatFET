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
SAMPLE_DELIVERY_TIMEOUT_MS   = 3000


def allocate_transfer_buffer(buffer_size):
    return array.array('B', bytes(buffer_size))


def read_rhododendron_m0_loadable():
    """ Read the contents of the default Rhododendron loadable from the tools distribution. """

    filename = os.getenv('RHODODENDRON_M0_BIN', find_greatfet_asset('rhododendron_m0.bin'))

    with open(filename, 'rb') as f:
        return f.read()


def main():

    SPEED_HIGH = 0
    SPEED_FULL = 1
    SPEED_LOW  = 2

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
                         help='Provide this option to write the raw binary samples to the standard out. Implies -q.')


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

    # Bring our Rhododendron board online; and capture communication parameters.
    buffer_size, endpoint = device.apis.usb_analyzer.initialize(args.speed, timeout=10000, comms_timeout=10000)

    # $Load the Rhododendron firmware loadable into memory...
    try:
        if args.m0:
            data = args.m0.read()
        else:
            data = read_rhododendron_m0_loadable()
    except (OSError, TypeError):
        log_error("Can't find a Rhododendron m0 program to load!")
        log_error("We can't run without one.")
        sys.exit(-1)

    # Debug only: setup a pin to track when we're handling SGPIO data.
    debug_pin = device.gpio.get_pin('J1_P3')
    debug_pin.set_direction(debug_pin.DIRECTION_OUT)

    # ... and then run it on our m0 coprocessor.
    device.m0.run_loadable(data)

    # Print what we're doing and our status.
    log_function("Reading raw high-speed USB data!\n")
    log_function("Press Ctrl+C to stop reading data from device.")

    # If we have a target binary file, open the target filename and use that to store samples.
    bin_file = None
    if args.binary:
        bin_file = open(args.binary, 'wb')
        bin_file_name = args.binary

    # Now that we're done with all of that setup, perform our actual sampling, in a tight loop,
    device.apis.usb_analyzer.start_capture()

    transfer_buffer = allocate_transfer_buffer(buffer_size)

    total_captured = 0

    try:
        while True:

            # Capture data from the device, and unpack it.
            try:
                new_samples = device.comms.device.read(endpoint, transfer_buffer, SAMPLE_DELIVERY_TIMEOUT_MS)
                samples = bytes(transfer_buffer[0:new_samples - 1])

                total_captured += new_samples
                log_function("Captured {} bytes.".format(total_captured), end="\r")


                # Output the samples to the appropriate targets.
                if args.binary:
                    bin_file.write(samples)
                if args.write_to_stdout:
                    sys.stdout.buffer.write(samples)
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

        if args.binary:
            log_function("Binary data written to file '{}'.".format(args.binary))


if __name__ == '__main__':
    main()
