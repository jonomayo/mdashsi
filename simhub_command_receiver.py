#!/usr/bin/env python3
"""Receive CYD button commands and send the configured game key to Windows."""

import json
import socket
from pynput.keyboard import Controller, Key

LISTEN_HOST = "0.0.0.0"
LISTEN_PORT = 9001

# Change these to match the keyboard bindings in your game.
COMMAND_KEYS = {
    "pit_limiter": "p",
    "headlights": "h",
    "wipers": "w",
    "ignition": "i",
    "starter": "s",
    "reset_car": "r",
}

keyboard = Controller()


def press_key(key_name: str) -> None:
    key = getattr(Key, key_name, key_name) if len(key_name) > 1 else key_name
    keyboard.press(key)
    keyboard.release(key)


def main() -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((LISTEN_HOST, LISTEN_PORT))
    print(f"Listening for CYD commands on UDP {LISTEN_PORT}")
    print("Keep the sim window focused so it receives the key presses.")

    while True:
        packet, address = sock.recvfrom(512)
        try:
            message = json.loads(packet.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            print(f"Ignoring invalid packet from {address[0]}")
            continue

        if message.get("type") != "simhub_command":
            continue

        command = message.get("command")
        key = COMMAND_KEYS.get(command)
        if key is None:
            print(f"Unknown command from {address[0]}: {command}")
            continue

        press_key(key)
        print(f"{command} -> {key}")


if __name__ == "__main__":
    main()
