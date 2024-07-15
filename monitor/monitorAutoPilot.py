#!/usr/local/bin/python3

import socket
import struct

DATA_SIZE = 300
BUFFER_SIZE = 1000
LOCAL_PORT = 8888

class AutoPilot:
    BATTERY_VOLTS_AVERAGE_MAX_SIZE = 12
    INPUT_VOLTS_AVERAGE_MAX_SIZE = 12

    def __init__(self):
        self.year = 0
        self.month = 0
        self.day = 0
        self.hour = 0
        self.minute = 0
        self.fix = False
        self.fixquality = 0
        self.satellites = 0
        self.mode = 0
        self.waypoint_set = False
        self.waypoint_lat = 0.0
        self.waypoint_lon = 0.0
        self.heading_desired = 0.0
        self.heading_long_average = 0.0
        self.heading_long_average_change = 0.0
        self.heading_long_average_size = 0
        self.heading_short_average = 0.0
        self.heading_short_average_change = 0.0
        self.heading_short_average_size = 0
        self.heading = 0.0
        self.bearing = 0.0
        self.bearing_correction = 0.0
        self.speed = 0.0
        self.distance = 0.0
        self.course = 0.0
        self.location_lat = 0.0
        self.location_lon = 0.0
        self.destinationChanged = True
        self.battery_voltage = 0.0
        self.battery_voltage_average_size = 0
        self.input_voltage = 0.0
        self.input_voltage_average_size = 0
        self.reset = False

    def parse(self, sentence):
        if sentence.startswith("APDAT,"):
            self.parseAPDAT(sentence)
        elif sentence.startswith("RESET,"):
            self.parseRESET(sentence)
        else:
            print("Unknown sentence")

    def parseAPDAT(self, sentence):
        fields = sentence.split(',')
        self.year = int(fields[1]) if fields[1] else 0
        self.month = int(fields[2]) if fields[2] else 0
        self.day = int(fields[3]) if fields[3] else 0
        self.hour = int(fields[4]) if fields[4] else 0
        self.minute = int(fields[5]) if fields[5] else 0
        self.fix = int(fields[6]) > 0 if fields[6] else False
        self.fixquality = int(fields[7]) if fields[7] else 0
        self.satellites = int(fields[8]) if fields[8] else 0
        self.mode = int(fields[9]) if fields[9] else 0
        self.waypoint_set = int(fields[10]) > 0 if fields[10] else False
        self.waypoint_lat = float(fields[11]) if fields[11] else 0.0
        self.waypoint_lon = float(fields[12]) if fields[12] else 0.0
        self.heading_desired = float(fields[13]) if fields[13] else 0.0
        self.heading_long_average = float(fields[14]) if fields[14] else 0.0
        self.heading_long_average_change = float(fields[15]) if fields[15] else 0.0
        self.heading_long_average_size = int(fields[16]) if fields[16] else 0
        self.heading_short_average = float(fields[17]) if fields[17] else 0.0
        self.heading_short_average_change = float(fields[18]) if fields[18] else 0.0
        self.heading_short_average_size = int(fields[19]) if fields[19] else 0
        self.heading = float(fields[20]) if fields[20] else 0.0
        self.bearing = float(fields[21]) if fields[21] else 0.0
        self.bearing_correction = float(fields[22]) if fields[22] else 0.0
        self.speed = float(fields[23]) if fields[23] else 0.0
        self.distance = float(fields[24]) if fields[24] else 0.0
        self.course = float(fields[25]) if fields[25] else 0.0
        self.location_lat = float(fields[26]) if fields[26] else 0.0
        self.location_lon = float(fields[27]) if fields[27] else 0.0

    def parseRESET(self, sentence):
        self.reset = True

    def printAutoPilot(self):
        print(f"Date&Time: {self.month}/{self.day}/{self.year:02d} {self.hour}:{self.minute:02d}")
        fixquality_desc = "n/a" if self.fixquality == 0 else "GPS" if self.fixquality == 1 else "DGPS"
        print(f"Fix: {fixquality_desc} ({self.satellites})" if self.fix else "No Fix")
        mode_desc = "navigate" if self.mode == 2 else "compass" if self.mode == 1 else "disabled" if self.mode == 0 else "N/A"
        destination = f"{self.waypoint_lat}, {self.waypoint_lon}" if self.mode == 2 else f"{self.heading_desired}" if self.mode == 1 else "N/A"
        print(f"Destination: {mode_desc} {destination}")
        print(f"Heading: {self.heading_long_average} ~ {self.heading_long_average_change} ({self.heading_long_average_size}) / "
              f"{self.heading_short_average} ~ {self.heading_short_average_change} ({self.heading_short_average_size}) / "
              f"{self.heading} Bearing: {self.bearing} {abs(self.bearing_correction)} {'R' if self.bearing_correction > 0 else 'L'}")
        print(f"Speed: {self.speed} Distance: {self.distance} Course: {self.course}")
        print(f"Location: {self.location_lat}, {self.location_lon}\n")

def main():
    # Set up UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('', LOCAL_PORT))
    print(f"Listening on UDP port {LOCAL_PORT}")

    buffer = ""

    while True:
        data, addr = sock.recvfrom(DATA_SIZE)
        packet = data.decode('utf-8')
        buffer += packet

        while True:
            start = buffer.find('~') + 1
            end = buffer.find('$')
            if start > 0 and end > 0:
                sentence = buffer[start:end]
                buffer = buffer[end+1:]
                autoPilot = AutoPilot()
                autoPilot.parse(sentence)
                autoPilot.printAutoPilot()
            else:
                break

if __name__ == "__main__":
    main()

