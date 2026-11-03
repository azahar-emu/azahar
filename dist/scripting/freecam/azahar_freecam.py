#!/usr/bin/env python3
"""
Free camera client for Azahar.

Reads a controller, moves a camera and sends the resulting matrix to Azahar's RPC
server, which multiplies the position of every vertex by it. The matrix is

    M = P * C^-1 * P^-1

where P is an assumed projection of the game (the settings below) and C is the camera transform.
Requires pygame: pip install pygame
"""

import math
import os
import socket
import struct
import sys
import time

# Azahar's RPC server
HOST = "127.0.0.1"
PORT = 45987

# Assumed projection of the game. A wrong guess still moves the camera, but distorts the scene.
# FOV_DEG, NEAR and FAR are the starting values, the controller adjusts them per scene
FOV_DEG = 55.0
ASPECT = 400.0 / 240.0
NEAR = 1.0
FAR = 1000.0

SCREEN_ROTATION_DEG = -90.0

# Leave vertices with w == 1 (most HUDs and 2D elements) in place
SKIP_2D = True

# Speeds, per second at full stick. Movement adjusts to the near value, so it follows the scene's scale
MOVE_SPEED = 5.0
TURN_SPEED_DEG = 120.0
FOV_SPEED_DEG = 20.0
NEAR_FAR_SPEED = 2.0  # factor per second
FAST_MULTIPLIER = 4.0
SLOW_MULTIPLIER = 0.2
DEADZONE = 0.15
UPDATE_HZ = 60

# Controller layout, as pygame joystick indices.
CONTROLLER_INDEX = 0
AXIS_MOVE_X = 0  # left stick
AXIS_MOVE_Y = 1
AXIS_LOOK_X = 3  # right stick
AXIS_LOOK_Y = 4
AXIS_SLOW = 2  # left trigger
AXIS_FAST = 5  # right trigger
BUTTON_DOWN = 4  # LB
BUTTON_UP = 5  # RB
BUTTON_RESET = 6  # Back
BUTTON_TOGGLE = 7  # Start
BUTTON_FAR_DOWN = 2  # X
BUTTON_FAR_UP = 3  # Y
HAT_TUNE = 0  # D-pad, up/down for the FOV, left/right for the near plane

# RPC protocol, see src/core/rpc/packet.h
RPC_VERSION = 2
PACKET_TYPE_SET_FREECAM = 8
FLAG_SKIP_2D = 1 << 0

IDENTITY = [[1.0 if r == c else 0.0 for c in range(4)] for r in range(4)]

def mat_mul(*mats):
    out = IDENTITY
    for m in mats:
        out = [[sum(out[r][k] * m[k][c] for k in range(4)) for c in range(4)] for r in range(4)]
    return out

def transpose(m):
    return [[m[c][r] for c in range(4)] for r in range(4)]

def translate(x, y, z):
    return [[1, 0, 0, x], [0, 1, 0, y], [0, 0, 1, z], [0, 0, 0, 1]]

def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return [[1, 0, 0, 0], [0, c, -s, 0], [0, s, c, 0], [0, 0, 0, 1]]

def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return [[c, 0, s, 0], [0, 1, 0, 0], [-s, 0, c, 0], [0, 0, 0, 1]]

def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return [[c, -s, 0, 0], [s, c, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]

def projection(fov_deg, near, far):
    f = 1.0 / math.tan(math.radians(fov_deg) / 2)
    a = far / (near - far)
    b = near * far / (near - far)
    persp = [[f / ASPECT, 0, 0, 0], [0, f, 0, 0], [0, 0, a, b], [0, 0, -1, 0]]
    persp_inv = [[ASPECT / f, 0, 0, 0], [0, 1 / f, 0, 0], [0, 0, 0, -1], [0, 0, 1 / b, a / b]]
    rotation = rot_z(math.radians(SCREEN_ROTATION_DEG))
    return mat_mul(rotation, persp), mat_mul(persp_inv, transpose(rotation))

class Camera:
    def __init__(self):
        self.fov_deg = FOV_DEG
        self.near = NEAR
        self.far = FAR
        self.proj, self.proj_inv = projection(self.fov_deg, self.near, self.far)
        self.reset()

    def adjust(self, fov, near, far, dt):
        if not (fov or near or far):
            return False
        self.fov_deg = max(5.0, min(170.0, self.fov_deg + fov * FOV_SPEED_DEG * dt))
        self.near *= NEAR_FAR_SPEED ** (near * dt)
        self.far = max(self.near * 1.01, self.far * NEAR_FAR_SPEED ** (far * dt))
        self.proj, self.proj_inv = projection(self.fov_deg, self.near, self.far)
        return True

    def str(self):
        return f"FOV_DEG = {self.fov_deg:.4g}, NEAR = {self.near:.4g}, FAR = {self.far:.4g}"

    def reset(self):
        self.pos = [0.0, 0.0, 0.0]
        self.yaw = 0.0
        self.pitch = 0.0

    def rotation(self):
        return mat_mul(rot_y(self.yaw), rot_x(self.pitch))

    def update(self, right, back, up, look_x, look_y, dt):
        turn = math.radians(TURN_SPEED_DEG) * dt
        self.yaw -= look_x * turn
        self.pitch = max(-1.55, min(1.55, self.pitch - look_y * turn))
        rot = self.rotation()
        local = [right * dt, up * dt, back * dt]  # -Z is forward
        for r in range(3):
            self.pos[r] += sum(rot[r][c] * local[c] for c in range(3))

    def matrix(self):
        view = mat_mul(transpose(self.rotation()), translate(*[-p for p in self.pos]))
        return mat_mul(self.proj, view, self.proj_inv)


def send_matrix(sock, matrix, skip_2d):
    payload = struct.pack("<II", FLAG_SKIP_2D if skip_2d else 0, 0)
    payload += struct.pack("<16f", *[matrix[r][c] for c in range(4) for r in range(4)])
    header = struct.pack("<IIII", RPC_VERSION, 0, PACKET_TYPE_SET_FREECAM, len(payload))
    sock.sendto(header + payload, (HOST, PORT))
    # Ignore replies
    try:
        while True:
            sock.recv(64)
    except BlockingIOError:
        pass


def main():
    # Keep Ctrl-C working, SDL replaces the handler otherwise
    os.environ.setdefault("SDL_NO_SIGNAL_HANDLERS", "1")
    try:
        import pygame
    except ImportError:
        print("pygame is required: pip install pygame")
        return 1

    pygame.init()
    if pygame.joystick.get_count() <= CONTROLLER_INDEX:
        print("no controller found")
        return 1
    pad = pygame.joystick.Joystick(CONTROLLER_INDEX)
    print(f"controller: {pad.get_name()}")
    print("Start: free camera on/off, Back: reset, D-pad and X/Y: FOV, near and far, Ctrl-C: quit")

    def axis(index):
        value = pad.get_axis(index)
        return value if abs(value) > DEADZONE else 0.0

    def trigger(index):
        return pad.get_axis(index) > 0.0  # -1 when released

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setblocking(False)
    camera = Camera()
    enabled = False
    previous_buttons = {}
    last_sent = None
    last_print = 0.0
    previous = time.perf_counter()

    def pressed(button):
        down = pad.get_button(button)
        was_down = previous_buttons.get(button, False)
        previous_buttons[button] = down
        return down and not was_down

    try:
        while True:
            pygame.event.pump()
            now = time.perf_counter()
            dt = min(now - previous, 0.1)
            previous = now

            if pressed(BUTTON_TOGGLE):
                enabled = not enabled
                camera.reset()
                print("free camera " + ("on" if enabled else "off"))
            if pressed(BUTTON_RESET):
                camera.reset()

            hat_x, hat_y = pad.get_hat(HAT_TUNE) if pad.get_numhats() > HAT_TUNE else (0, 0)
            far = pad.get_button(BUTTON_FAR_UP) - pad.get_button(BUTTON_FAR_DOWN)
            if camera.adjust(hat_y, hat_x, far, dt) and now - last_print > 0.2:
                print(camera.str())
                last_print = now

            if enabled:
                speed = MOVE_SPEED * camera.near
                if trigger(AXIS_FAST):
                    speed *= FAST_MULTIPLIER
                if trigger(AXIS_SLOW):
                    speed *= SLOW_MULTIPLIER
                up = pad.get_button(BUTTON_UP) - pad.get_button(BUTTON_DOWN)
                camera.update(axis(AXIS_MOVE_X) * speed, axis(AXIS_MOVE_Y) * speed, up * speed,
                              axis(AXIS_LOOK_X), axis(AXIS_LOOK_Y), dt)
                matrix = camera.matrix()
            else:
                matrix = IDENTITY

            if matrix != last_sent:
                send_matrix(sock, matrix, SKIP_2D)
                last_sent = matrix

            time.sleep(max(0.0, 1.0 / UPDATE_HZ - (time.perf_counter() - now)))
    except KeyboardInterrupt:
        pass
    finally:
        print("last projection: " + camera.str())
        send_matrix(sock, IDENTITY, False)
        pygame.quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
