import os
import sys

# Make the emulator modules (nmea, geo, garmin_emulator) importable from tests/.
sys.path.insert(0, os.path.dirname(__file__))
