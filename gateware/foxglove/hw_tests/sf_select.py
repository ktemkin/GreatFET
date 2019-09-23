
import math

from nmigen import Signal, Module, Elaboratable, ClockDomain, ClockSignal, Array, Cat
from greatfet.neighbors.foxgen.platform.foxglove_r0_1 import FoxglovePlatformR01


class PortASpecialFunctionRegisters(Elaboratable):
    """ Special function registers that drive each of the select/direction lines. """

    def __init__(self, port_width=8):
        """ Define our I/O. """

        #
        # Inputs
        #
        self.bit_number     = Signal(3)
        self.select_in      = Signal(3)
        self.direction_in   = Signal()
        self.write_strobe   = Signal()

        #
        # Outputs   
        #
    
        # Create the buffers for connection and direction control.
        self.select_lines    = Array(Signal(3) for _ in range(port_width))
        self.direction_lines = Array(Signal()  for _ in range(port_width))


    def elaborate(self):
        m = Module()

        # Update the relevant registers whenever the write_strobe is set.
        with m.If(self.write_strobe):
            self.select_lines[self.bit_number].eq(self.select_in)
            self.direction_lines[self.bit_number].eq(self.direction_in)

        return m



class SimpleSPIReciever(Elaborable):
    """ Class that converts SPI commands to register writes. """


    def __init__(self, address_width, register_width):

        #
        # SPI Bus
        #
        self.sck  = Signal()
        self.miso = Signal()
        self.mosi = Signal()
        self.cs_n = Signal()

        #
        # Outputs   
        #
        
        self.register_number = Signal(address_width)
        self.register_value  = Signal(register_width)
        self.new_data        = Signal()


        # Capture our arguments.
        self._address_width = address_width
        self._register_width = register_width



    @staticmethod
    def rising_edge_detector(m, input_signal)
    """ Returns a signal that's true when an input signal has experienced a rising edge. """

        output        = Signal()
        delay_circuit = Signal(2)

        # Create our time-delayed version of our input signal.
        m.d.sync += delay_circuit.eq(Cat(input_signal, delay_circuit[0]))

        # ... and detect edges on it.
        m.d.comb += output.eq((delayed[1] == 1) & (delayed[0] == 0))

        return output



    def elaborate(self):
        m = Module()

        # Create our receive shift register.
        sck_edge       = self.rising_edge_detector(self.sck)
        shift_register = Signal(address_width + register_width)
        bit_counter    = Signal(max=address_width + register_width)

        # Trigger our write strobe for the single cycle where we've reached our maximum bit count.
        self.d.comb += self.new_data.eq(bit_counter == (self._address_width + self._register_width))

        # If the chip select is active...
        with m.If(~self.cs_n):

            # ... shift on each rising edge of SCK.
            with m.If(sck_edge):
                self.d.sync += shift_register.eq(Cat(self.shift_register[0:-1], self.mosi))
                self.d.sync += bit_counter.eq(self.bit_counter + 1)


            # If we've just shifted in the last bit, load our output registers with the received data,
            # and reset our shift counter.
            with m.If(self.new_data)
                self.d.sync += self.register_number.eq(self.shift_register[0:self._address_width])
                self.d.sync += self.register_value.eq(self.shift_register[self._address_with:self._address_width + self._register_width])
                self.d.sync += bit_counter.eq(0)


        # ... otherwise, clear our bit counter.
        with m.Else():
            self.d.sync += self.bit_counter.eq(0)
            
            
        return m
    


class SimpleSPIRegisters(Elaborable):
    """ SPI register set that allows us to control the SF selection on each of the registers. """

        #
        # Inputs
        #
        self.bit_number     = Signal(3)
        self.select_in      = Signal(3)
        self.direction_in   = Signal()
        self.write_strobe   = Signal()

        #
        # Outputs   
        #
    
        # Create the buffers for connection and direction control.
        self.select_lines    = Array(Signal(3) for _ in range(port_width))
        self.direction_lines = Array(Signal()  for _ in range(port_width))



class SpecialFunctionTester(Elaboratable):
    """ Hardware module that tests each of Foxglove's Port A special functions. """


    def elaborate(self, platform):
        """ Generate the SF tester. """

        m = Module()

        # Grab our I/O connectors.
        clk = platform.request("clk2")
        port_b = platform.request("port_b")

        # Grab our clock signal, and attach it to the main clock domain.
        m.domains.sync = ClockDomain()
        m.d.comb = ClockSignal().eq(clk)

        # Increment port_b every clock cycle, for now.
        m.d.sync += port_b.eq(port_b + 1)


if __name__ == "__main__":
    platform = FoxglovePlatformR01()
    platform.build(SpecialFunctionTester, do_program=True)
