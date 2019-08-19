""" tools for working with GreatFET boards from python

To use these tools programmatically, usually you'll want to use the GreatFET()
factory function, which will automatically connect to the first available GreatFET
board. For example:

    from greatfet import GreatFET
    gf = GreatFET()

You can also limit the boards connected to by serial number:

    import greatfet
    gf = greatfet.GreatFET(serial_number="000041a465d9344a2617")

You can also get a list of all connected GreatFETs by using the find_all argument:

    import greatfet
    gf = greatfet.GreatFET(find_all=True)


You can find more information on the features supported by the relevant GreatFET by calling
help on one of your GreatFET objects:

    gf = greatfet.GreatFET()
    help(gf)

You can also request help on properties of the individual GreatFET objects;

    help(gf.leds[1])
    help(gf.gpio)


"""

from __future__ import print_function
# Alias objects to make them easier to import.

from .greatfet import GreatFET
GreatFET = GreatFET  # pyflakes

def greatfet_assets_directory():
    """ Provide a quick function that helps us get at our assets directory. """
    import os

    # Find the path to the module, and then find its assets folder.
    module_path = os.path.dirname(__file__)
    return os.path.join(module_path, 'assets')


def find_greatfet_asset(filename):
    """ Returns the path to a given GreatFET asset, if it exists, or None if the GreatFET asset isn't provided."""
    import os

    asset_path = os.path.join(greatfet_assets_directory(), filename)

    if os.path.isfile(asset_path):
        return asset_path
    else:
        return None
