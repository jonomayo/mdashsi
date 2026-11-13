#!/usr/bin/env python3
import json
import socket
import time

HOST = "192.168.68.67"
PORT = 9000

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

rpm = 1200
gear = 1
speed = 25.0
lap = 82.154

try:
    while True:
        payload = {
            "rpm": rpm,
            "gear": gear,
            "speed": speed,
            "lapTime": lap,
        }
        sock.sendto(json.dumps(payload).encode("utf-8"), (HOST, PORT))
        print(json.dumps(payload))

        rpm += 140
        if rpm > 7800:
            rpm = 1200

        gear = 1 if speed < 35 else 2 if speed < 65 else 3 if speed < 95 else 4 if speed < 120 else 5
        speed = speed + 2.6
        if speed > 170:
            speed = 25.0

        lap = lap + 0.05
        if lap > 120:
            lap = 82.154

        time.sleep(0.1)
except KeyboardInterrupt:
    print("\nStopping SimHub test sender")
