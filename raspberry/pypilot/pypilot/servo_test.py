#!/usr/bin/env python
#
#   Copyright (C) 2021 Sean D'Epagnier
#
# This Program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 3 of the License, or (at your option) any later version.  

import os
import sys
import time 
from datetime import datetime

sys.path.append(os.path.dirname(os.path.abspath(__file__)))

class Servo(object):
    def __init__(self):
        from pypilot.wifi_servo.wifi_servo import WifiServo
        self.driver = WifiServo()

        self.watch_values = {}
        self.watch_values['ap.enabled'] = False
        self.watch_values['ap.heading'] = 0
        self.watch_values['ap.heading_command'] = 0
        self.watch_values['ap.mode'] = 'compass'

    def send_heading(self):
        result = self.driver.heading(self.watch_values['ap.heading'])
        if result != 'ok':
            print(f'Error setting heading: {result}')

    def send_track(self):
        result = self.driver.track(self.watch_values['ap.heading_command'])
        print(f'Sent Track got response [{result}]\n')
        if result != 'ok':
            if result[0] == 't':
                track_adjust = float(result[1:len(result)])
                # If we want to alter where we are going track no matter means
                # anything to us.  We need to change mode to compass and point to
                # where we are pointing.
                # Note: I'm adjusting based on heading and not track is that accurate?
                new_track = self.watch_values['ap.heading'] + track_adjust
                print(f'Set mode to compass and track to [{new_track}]\n')
            else:
                print(f'Error setting track: {result}')

    def send_mode(self):
        result = self.driver.mode(self.watch_values['ap.mode'])
        print(f'Sent Mode got response [{result}]\n')
        print(result)
        if result != 'ok':
            print("Result is not 'ok'")
            if result[0] == 'm':
                # skip the leading m and the other for the trailing CR/LF
                mode_adjust = result[1:-1]
                print(f'modeadjust = {mode_adjust}')
                if mode_adjust == "compass" or mode_adjust == "gps":
                    print(f'Set mode to [{mode_adjust}]\n')
            else:
                print(f'Error setting mode: {result}')
        else:
            print("Result is 'ok'")

    def send_enabled(self):
        result = self.driver.enabled(1 if self.watch_values['ap.enabled'] else 0)
        print(f'Sent Enabled got response [{result}]\n')
        if result != 'ok':
            if result[0] == 'e':
                enabled_adjust = int(result[1:len(result)])
                print(f'Set enabled to [{enabled_adjust}]\n')
            else:
                print(f'Error setting enabled: {result}')

    def poll(self):
        print("Polling\n")
        self.send_heading()
        self.send_track()
        self.send_mode()
        self.send_enabled()

def main():
    print('wifi Servo test')

    servo = Servo()

    period = 1
    lastt = time.monotonic()
    while True:
        servo.poll()

        dt = period - time.monotonic() + lastt
        if dt > 0 and dt < period:
            time.sleep(dt)
            lastt += period
        else:
            lastt = time.monotonic()


if __name__ == '__main__':
    main()
