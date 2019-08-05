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

from greatfet import GreatFET
from greatfet.utils import GreatFETArgumentParser, log_silent, log_error

# Default sample-delivery timeout.
SAMPLE_DELIVERY_TIMEOUT_MS   = 3000

# Default number of pre-allocated buffers.
DEFAULT_PREALLOCATED_BUFFERS = 4096



def background_process_data(termination_request, args, bin_file, empty_buffers, full_buffers):
    """ Thread that handles processing our samples in the background. """

    # Process in the background until we're explicitly terminated.
    while True:

        # If we have nothing to do, check to see if it's time for us to stop.
        # If it isn't, keep looping.
        if len(full_buffers) == 0:
            if termination_request.is_set():
                break
            else:
                # Sleep a very short while before we return to the beginning of the loop
                # this momentarily yields back to the thread scheduler.
                time.sleep(0.0001)
                continue

        if termination_request.is_set():
            if args.verbose:
                sys.stderr.write("{} buffers remaining...\r".format(len(full_buffers)))
                sys.stderr.flush()

        samples = bytes(full_buffers.pop(0))

        # Output the samples to the appropriate targets.
        if args.binary:
            bin_file.write(samples)
        if args.write_to_stdout:
            sys.stdout.write(samples)

        # ... and add the buffer back to our empty list.
        empty_buffers.append(active_buffer)


def allocate_transfer_buffer(buffer_size):
    return array.array('B', b"\0" * buffer_size)


def main():

    # Set up our argument parser.
    parser = GreatFETArgumentParser(description="Simple Rhododendron capture utility for GreatFET.", verbose_by_default=True)
    parser.add_argument('-o', '-b', '--binary', dest='binary', metavar='<filename>', type=str,
                        help="Write the raw samples captured to a file with the provided name.")
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
    buffer_size, endpoint = device.apis.usb_analyzer.initialize()

    # Print what we're doing and our status.
    log_function("Reading raw high-speed USB data!\n")
    log_function("Press Ctrl+C to stop reading data from device.")

    # If we have a target binary file, open the target filename and use that to store samples.
    if args.binary:
        bin_file = open(args.binary, 'wb')
        bin_file_name = args.binary

    # Create queues of transfer objects that we'll use as a producer/consumer interface for our comm thread.
    empty_buffers = []
    full_buffers  = []

    # Allocate a set of transfer buffers, so we don't have to continuously allocate them.
    for _ in range(DEFAULT_PREALLOCATED_BUFFERS):
        empty_buffers.append(allocate_transfer_buffer(buffer_size))

    # Finally, spawn the thread that will handle our data processing and output.
    termination_request = threading.Event()
    thread_arguments    = (termination_request, args, bin_file, empty_buffers, full_buffers)
    data_thread         = threading.Thread(target=background_process_data, args=thread_arguments)

    # Now that we're done with all of that setup, perform our actual sampling, in a tight loop,
    data_thread.start()
    device.apis.usb_analyzer.start_capture()
    start_time = time.time()

    try:
        while True:

            # Grab a transfer buffer from the empty list...
            try:
                transfer_buffer = empty_buffers.pop()
            except IndexError:
                # If we don't have a buffer to fill, allocate a new one. It'll wind up in our buffer pool shortly.
                transfer_buffer = allocate_transfer_buffer(buffer_size)

            # Capture data from the device, and unpack it.
            device.comms.device.read(endpoint, transfer_buffer, 3000)

            # ... and pop it into the to-be-processed queue.
            full_buffers.append(transfer_buffer)

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
        elapsed_time = time.time() - start_time

        # No matter what, once we're done stop the device from sampling.
        device.apis.logic_analyzer.stop()

        # Signal to our data processing thread that it's time to terminate.
        termination_request.set()

    # Wait for our data processing thread to complete.
    log_function('')
    log_function('Capture terminated -- waiting for data processing to complete.')
    data_thread.join()

    # Finally, generate our output.
    if args.binary:
        log_function("Binary data written to file '{}'.".format(args.binary))

    # Print how long we sampled for, as a nicety.
    log_function("Sampled for {} seconds.".format(round(elapsed_time, 4)))


if __name__ == '__main__':
    main()
