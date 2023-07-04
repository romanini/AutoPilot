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
from datetime import datetime

sys.path.append(os.path.dirname(os.path.abspath(__file__)))

from values import *

def sign(x):
    if x > 0:
        return 1
    if x < 0:
        return -1
    return 0

def interpolate(x, x0, x1, y0, y1):
    d = (x-x0)/(x1-x0)
    return (1-x) * y0 + d * y1

# a property which records the time when it is updated
class TimedProperty(Property):
    def __init__(self, name):
        super(TimedProperty, self).__init__(name, 0)
        self.time = 0

    def set(self, value):
        self.time = time.monotonic()
        return super(TimedProperty, self).set(value)

class TimeoutSensorValue(SensorValue):
    def __init__(self, name):
        super(TimeoutSensorValue, self).__init__(name, False, fmt='%.3f')

    def set(self, value):
        self.time = time.monotonic()
        super(TimeoutSensorValue, self).set(value)

    def timeout(self):
        if self.value and time.monotonic() - self.time > 8:
            self.set(False)

# range setting bounded pairs, don't let max setting below min setting, and ensure max is at least min
class MinRangeSetting(RangeSetting):
    def __init__(self, name, initial, min_value, max_value, units, minvalue, **kwargs):
        self.minvalue = minvalue
        minvalue.maxvalue = self
        super(MinRangeSetting, self).__init__(name, initial, min_value, max_value, units, **kwargs)

    def set(self, value):
        if value < self.minvalue.value:
            value = self.minvalue.value
        super(MinRangeSetting, self).set(value)

class MaxRangeSetting(RangeSetting):
    def __init__(self, name, initial, min_value, max_value, units, **kwargs):
        self.maxvalue = None
        super(MaxRangeSetting, self).__init__(name, initial, min_value, max_value, units, **kwargs)

    def set(self, value):
        if self.maxvalue and value > self.maxvalue.value:
            self.maxvalue.set(value)
        super(MaxRangeSetting, self).set(value)
            
class Servo(object):
    pypilot_dir = os.getenv('HOME') + '/.pypilot/'
    calibration_filename = pypilot_dir + 'servocalibration'

    command_log_filename = pypilot_dir + 'servocommand.log'

    def __init__(self, client, sensors):
        self.command_log = open(Servo.command_log_filename, 'a')

        self.client = client
        self.sensors = sensors
        self.lastdir = 0 # doesn't matter

        self.calibration = self.register(JSONValue, 'calibration', {})
        self.load_calibration()

        self.position_command = self.register(TimedProperty, 'position_command')
        self.command = self.register(TimedProperty, 'command')

        self.faults = self.register(ResettableValue, 'faults', 0, persistent=True)

        self.engaged = self.register(BooleanValue, 'engaged', False)
        self.brake_on = False

        self.speed = self.register(SensorValue, 'speed')
        self.speed.min = self.register(MaxRangeSetting, 'speed.min', 100, 0, 100, '%')
        self.speed.max = self.register(MinRangeSetting, 'speed.max', 100, 0, 100, '%', self.speed.min)

        self.position = self.register(SensorValue, 'position')
        self.position.elp = 0
        self.position.set(0)
        self.position.p = self.register(RangeProperty, 'position.p', .15, .01, 1, persistent=True)
        self.position.i = self.register(RangeProperty, 'position.i', 0, 0, .1, persistent=True)
        self.position.d = self.register(RangeProperty, 'position.d', .02, 0, .1, persistent=True)

        self.rawcommand = self.register(SensorValue, 'raw_command')

        self.inttime = 0

        self.disengaged = True
        self.disengage_on_timeout = self.register(BooleanValue, 'disengage_on_timeout', True, persistent=True)
        self.force_engaged = False

        self.last_zero_command_time = self.command_timeout = time.monotonic()
        self.driver_timeout_start = 0

        self.state = self.register(StringValue, 'state', 'none')

        self.controller = self.register(StringValue, 'controller', 'arduino')

        self.poll_count = 0

        from client import pypilotClient

        self.local_client = pypilotClient("localhost")
        self.watch_values = {}
        self.watch_values['ap.enabled'] = False
        self.watch_values['ap.heading'] = 0
        self.watch_values['ap.heading_command'] = 0
        self.watch_values['ap.mode'] = ''

        self.watch_list = ['ap.enabled', 'ap.heading', 'ap.heading_command', 'ap.mode']

        for name in self.watch_list:
            self.local_client.watch(name)

        from pypilot.wifi_servo.wifi_servo import WifiServo
        self.driver = WifiServo()

        self.raw_command(0)

    def register(self, _type, name, *args, **kwargs):
        return self.client.register(_type(*(['servo.' + name] + list(args)), **kwargs))

    def log_command(self, line):
        # self.command_log.write(str(datetime.now()) + ' ' + line)
        pass

    def send_command(self):
        # self.log_command('send_command called with command: ' + str(self.command.value) + '\n')
        t = time.monotonic()

        if not self.disengage_on_timeout.value:
            self.disengaged = False

        t = time.monotonic()
        dp = t - self.position_command.time
        dc = t - self.command.time

        if dp < dc and not self.sensors.rudder.invalid():
            timeout = 10 # position command will expire after 10 seconds
            self.disengaged = False
            if abs(self.position.value - self.command.value) < 1:
                # self.log_command('setting command to 0 because position - command < 1\n')
                self.command.set(0)
            else:
                # self.log_command('send_command calling do_position_command with position_command: ' + str(self.position_command.value) + '\n')
                self.do_position_command(self.position_command.value)
                return
        elif self.command.value and not self.fault():
            timeout = 1 # command will expire after 1 second
            if time.monotonic() - self.command.time > timeout:
                #print('servo command timeout', time.monotonic() - self.command.time)
                # self.log_command('setting command to 0 because command has expired\n')
                self.command.set(0)
            self.disengaged = False
        # self.log_command('send_command calling do_command with command: ' + str(self.command.value) + '\n')
        self.do_command(self.command.value)
        
    def do_position_command(self, position):
        e = position - self.position.value
        d = self.speed.value * self.sensors.rudder.range.value

        self.position.elp = .98*self.position.elp + .02*min(max(e, -30), 30)
        #self.position.dlp = .8*self.position.dlp + .2*d

        p = self.position.p.value*e
        i = self.position.i.value*self.position.elp
        d = self.position.d.value*d
        pid = p + i + d
        #print('pid', pid, p, i, d)
        # map in min_speed to max_speed range
        # self.log_command('do_position_command calling do_command with command: ' + str(pid) + '\n')
        self.do_command(pid)

    def do_command(self, command):
        # self.log_command('do_command called with speed: ' + str(command) + '\n')
        #clamp to between -1 and 1 and round it to 2 decimal places to reduce noise.
        command = round(min(max(command, -1),1),2)
        # self.log_command('RAW_COMMAND('+ str(command) + ')\n')
        return self.raw_command(command)

    def stop(self):
        self.brake_on = False
        self.do_raw_command(0)
        self.lastdir = 0
        self.state.update('stop')

    def raw_command(self, command):
        # apply command before other calculations
        # self.brake_on = self.use_brake.value
        # self.log_command('DOING RAW COMMAND ' + str(command) + '\n\n')

        result = self.do_raw_command(command)

        if command <= 0:
            if command < 0:
                self.state.update('reverse')
                self.lastdir = -1
            else:
                self.speed.set(0)
                if self.brake_on:
                    self.state.update('brake')
                else:
                    self.state.update('idle')
        else:
            self.state.update('forward')
            self.lastdir = 1
        return result

    def do_raw_command(self, command):
        self.rawcommand.set(command)

        t = time.monotonic()
        if command == 0:
            # only send at .2 seconds when command is zero for more than a second
            if t > self.command_timeout + 1 and t - self.last_zero_command_time < .2:
                return
            self.last_zero_command_time = t
        else:
            self.command_timeout = t

        return self.driver.wheel(command)

    def send_heading(self):
        result = self.driver.heading(self.watch_values['ap.heading'])
        if result != 'ok':
            print(f'Error setting heading: {result}')

    def send_track(self):
        result = self.driver.track(self.watch_values['ap.heading_command'])
        # self.log_command(f'Sent Track got response [{result}]\n')
        if result != 'ok':
            if result[0] == 't':
                track_adjust = float(result[1:len(result)])
                # If we want to alter where we are going track no matter means
                # anything to us.  We need to change mode to compass and point to
                # where we are pointing.
                # Note: I'm adjusting based on heading and not track is that accurate?
                new_track = self.watch_values['ap.heading'] + track_adjust
                self.local_client.set('ap.mode', 'compass')
                self.local_client.set('ap.heading_command', new_track)
                # self.log_command(f'Set mode to compass and track to [{new_track}]\n')
            else:
                print(f'Error setting track: {result}')

    def send_mode(self):
        result = self.driver.mode(self.watch_values['ap.mode'])
        self.log_command(f'Sent Mode got response [{result}]\n')
        if result != 'ok':
            if result[0] == 'm':
                # skip the leading m and the other for the trailing CR/LF
                mode_adjust = result[1:-1]
                if mode_adjust == "compass" or mode_adjust == "gps":
                    self.local_client.set('ap.mode', mode_adjust)
                    self.log_command(f'Set mode to [{mode_adjust}]\n')
            else:
                print(f'Error setting mode: {result}')

    def send_enabled(self):
        result = self.driver.enabled(1 if self.watch_values['ap.enabled'] else 0)
        # self.log_command(f'Sent Enabled got response [{result}]\n')
        if result != 'ok':
            if result[0] == 'e':
                enabled_adjust = int(result[1:len(result)])
                self.local_client.set('ap.enabled', True if enabled_adjust == 1 else False)
                # self.log_command(f'Set enabled to [{enabled_adjust}]\n')
            else:
                print(f'Error setting enabled: {result}')

    def poll(self):
        self.send_command()
        self.poll_count += 1

        msgs = self.local_client.receive()
        for name, value in msgs.items():
            self.watch_values[name] = value
        # self.log_command(f'Watch Values: {self.watch_values}\n')

        self.send_heading()
        self.send_track()
        self.send_mode()
        self.send_enabled()

    def fault(self):
        return self.driver.fault()

    def load_calibration(self):
        import pyjson
        try:
            filename = Servo.calibration_filename
            print('loading servo calibration' + filename)
            file = open(filename)
            self.calibration.set(pyjson.loads(file.readline()))
        except:
            print('WARNING: using default servo calibration!!')
            self.calibration.set(False)

    def save_calibration(self):
        file = open(Servo.calibration_filename, 'w')
        file.write(pyjson.dumps(self.calibration))

def test(servo):
    result = servo.do_command(0.5)
    r = 0
    if result != 'ok':
        print('command sent to arduino servo failed: ' + result)
        r = 1

    print('command sent to arduino servo successfully')
    servo.poll()

    return -r

def main():
    print('pypilot Servo')
    from server import pypilotServer
    server = pypilotServer()

    from client import pypilotClient
    client = pypilotClient(server)

    from sensors import Sensors # for rudder feedback
    sensors = Sensors(client, False)
    servo = Servo(client, sensors)

    for i in range(len(sys.argv)):
        if sys.argv[i] == '-t':
            r = test(servo)
            exit(r)

    period = .1
    lastt = time.monotonic()
    while True:
        servo.poll()
        sensors.poll()
        client.poll()
        server.poll()

        dt = period - time.monotonic() + lastt
        if dt > 0 and dt < period:
            time.sleep(dt)
            lastt += period
        else:
            lastt = time.monotonic()


if __name__ == '__main__':
    main()
