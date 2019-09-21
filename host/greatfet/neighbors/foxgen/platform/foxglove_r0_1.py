#
# This file is part of GreatFET.
#

from nmigen.build import *
from nmigen.vendor.lattice_ecp5 import *

from .... import GreatFET

__all__ = ["FoxglovePlatformR01"]


class FoxglovePlatformR01(LatticeECP5Platform):
    """ Board description for the pre-release r0.1 (29-July-2019) revision of Foxglove. """

    device      = "LFE5UM-12F"
    package     = "BG256"
    speed       = "6"

    #default_clk = "clk100"

    resources   = [

        # Note that an awkward design decision by another engineer led to the ports
        # being numbered 1-8, matching their connector pins, rather than 0-7, which
        # matches the usual vector conventions. I'm choosing to enter these as 0-7,
        # as I plan on changing that in future silkscreen revisions. ~KT

        # Main connector for Port A; before the level shifter. The logic here is
        # always 3V3; the other side's voltage is set by the Vtarget regulator.
        Resource("port_a", 0, Subsignal("io", Pins("K16", dir="io")), Subsignal("oe", Pins("L14", dir="o")),
                Subsignal("connections", Pins("K14", "K15", "K16", dir="o", assert_width=3)), Attrs(IO_TYPE="LVCMOS33")),
        Resource("port_a", 1, Subsignal("io", Pins("J14", dir="io")), Subsignal("oe", Pins("J13", dir="o")),
                Subsignal("connections", Pins("G16", "H15", "J15", dir="o", assert_width=3)), Attrs(IO_TYPE="LVCMOS33")),
        Resource("port_a", 2, Subsignal("io", Pins("K13", dir="io")), Subsignal("oe", Pins("L15", dir="o")),
                Subsignal("connections", Pins("L16  M14  L13", dir="o", assert_width=3)), Attrs(IO_TYPE="LVCMOS33")),
        Resource("port_a", 3, Subsignal("io", Pins("G13", dir="io")), Subsignal("oe", Pins("M14", dir="o")),
                Subsignal("connections", Pins("G14  G15  F16", dir="o", assert_width=3)), Attrs(IO_TYPE="LVCMOS33")),
        Resource("port_a", 4, Subsignal("io", Pins("F15", dir="io")), Subsignal("oe", Pins("F14", dir="o")),
                Subsignal("connections", Pins("C15  D12  E12", dir="o", assert_width=3)), Attrs(IO_TYPE="LVCMOS33")),
        Resource("port_a", 5, Subsignal("io", Pins("E13", dir="io")), Subsignal("oe", Pins("E14", dir="o")),
                Subsignal("connections", Pins("D16  E15  E16", dir="o", assert_width=3)), Attrs(IO_TYPE="LVCMOS33")),
        Resource("port_a", 6, Subsignal("io", Pins("P13", dir="io")), Subsignal("oe", Pins("P12", dir="o")),
                Subsignal("connections", Pins("P14  P16  N11", dir="o", assert_width=3)), Attrs(IO_TYPE="LVCMOS33")),
        Resource("port_a", 7, Subsignal("io", Pins("N16", dir="io")), Subsignal("oe", Pins("N14", dir="o")),
                Subsignal("connections", Pins("M13  M15  M16", dir="o", assert_width=3)), Attrs(IO_TYPE="LVCMOS33")),


        # Main connector for Port B. The VCCIO for this logic can be set to a variety
        # of levels by software; so these attrs may need to be modified by software.
        Resource("port_b", 0, Pins("G2", dir="io"), Attrs(IO_TYPE="LVCMOS33")),
        Resource("port_b", 1, Pins("F1", dir="io"), Attrs(IO_TYPE="LVCMOS33")),
        Resource("port_b", 2, Pins("H2", dir="io"), Attrs(IO_TYPE="LVCMOS33")),
        Resource("port_b", 3, Pins("G1", dir="io"), Attrs(IO_TYPE="LVCMOS33")),
        Resource("port_b", 4, Pins("H5", dir="io"), Attrs(IO_TYPE="LVCMOS33")),
        Resource("port_b", 5, Pins("H4", dir="io"), Attrs(IO_TYPE="LVCMOS33")),
        Resource("port_b", 6, Pins("J2", dir="io"), Attrs(IO_TYPE="LVCMOS33")),
        Resource("port_b", 7, Pins("J1", dir="io"), Attrs(IO_TYPE="LVCMOS33")),


        # The pins in Port B are routed to also support being used differentially.
        # Accordingly, we provide a duplicate with differential naming.
        Resource("port_b_diff", 0, DiffPairs("F1", "G2", dir="io"), Attrs(IO_TYPE="LVCMOS33D")),
        Resource("port_b_diff", 1, DiffPairs("G1", "H2", dir="io"), Attrs(IO_TYPE="LVCMOS33D")),
        Resource("port_b_diff", 2, DiffPairs("H4", "H5", dir="io"), Attrs(IO_TYPE="LVCMOS33D")),
        Resource("port_b_diff", 3, DiffPairs("J1", "J2", dir="io"), Attrs(IO_TYPE="LVCMOS33D")),

        # ADC scaling control.
        Resource("adc_pull_en",     0, PinsN("P3", dir="o"), Attrs(IO_TYPE="LVCMOS33")),
        Resource("adc_pull_up",     0,  Pins("P4", dir="o"), Attrs(IO_TYPE="LVCMOS33"))

        # TODO: add aliases for CLK0/1, I2C, SSP, SPI, UART pins
    ]

    # Neighbor headers.
    connectors = [

        # Top header (J2)
        Connector("J", 2, """
        -   D7  D6  E5  E8  E9  E10  C4  C5  C6  C7  C8  C9  C10  C11  C12  C13  C14  M3   C16
        -   E6  -   D4  D8  D9  B3   B4  B5  B6  B7  B8  B9  B10  B11  B12  B13  B14  B15  B16
        """),

        # Bonus row (J7)
        Connector("J", 7, """
        -  E7  E4  D5  A2  A3  A4  A5  A6  A7  A8  A9  A10  A11  A12  A13  A14  A15  -  -
        """),

        # Bottom header (J1)
        Connector("J", 1, """
        -  L1  M2  N3  P2  -   R1  T2  T3  T4  T6  N8  R12  -  T13  T14  T15  P15  -   T7
        -  K4  -   M1  N1  P1  -   R2  R3  R4  R5  R6  -    -  R13  R14  R15  R16  R8  T8
        """),

    ]


    def toolchain_program(self, products, name):
        """ Programs the relevant Foxglove board with the generated build product. """

        # Create our connection to our Foxglove target.
        gf = GreatFET()
        foxglove = gf.attach_neighbor('foxglove')

        # Grab our generated bitstream, and upload it to the FPGA.
        bitstream =  products.get("{}.bit".format(name))
        foxglove.configure_fpga(bitstream)


