# Copyright 2018-2026 Citra Emulator Project / Azahar Emulator Project
# Licensed under GPLv2 or any later version
# Refer to the license.txt file included.

import struct
import random
import enum
import socket

CURRENT_REQUEST_VERSION = 2
MAX_REQUEST_DATA_SIZE = 32 * 1024
HEADER_SIZE = 0x10
MAX_PACKET_SIZE = MAX_REQUEST_DATA_SIZE + HEADER_SIZE

class RequestType(enum.IntEnum):
    ReadMemory = 1,
    WriteMemory = 2,
    ProcessList = 3,
    SetGetProcess = 4,
    TakeScreenshot = 5,
    ReadScreenshot = 6,
    GetPerfStats = 7,

class ScreenshotResult(enum.IntEnum):
    Success = 0,
    Unsupported = 1,
    Busy = 2,
    Timeout = 3,
    EncodeFailed = 4,
    InvalidArgument = 5,

SCREENSHOT_FLAG_SECONDARY_WINDOW = 1 << 0
SCREENSHOT_FLAG_RAW_RGB = 1 << 1

PERF_STATS_FIELDS = ("system_fps", "game_fps", "time_vblank_interval", "time_hle_svc",
                     "time_hle_ipc", "time_gpu", "time_swap", "time_remaining",
                     "emulation_speed", "artic_transmitted", "artic_events")

CITRA_PORT = 45987

class AzaharRPC:
    def __init__(self, address="127.0.0.1", port=CITRA_PORT, timeout=None, retries=3):
        """
        UDP may drop packets, and a lost request or reply would block forever.
        When connecting over a network, pass a timeout (in seconds) and requests are
        resent up to retries times.
        Every request except taking a screenshot is safe to resend.
        """
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.timeout = timeout
        self.retries = retries
        # Adjust socket buffer sizes to the max packet size
        for option in (socket.SO_SNDBUF, socket.SO_RCVBUF):
            if self.socket.getsockopt(socket.SOL_SOCKET, option) < MAX_PACKET_SIZE * 2:
                self.socket.setsockopt(socket.SOL_SOCKET, option, MAX_PACKET_SIZE * 2)
        self.address = address
        self.port = port
        self.max_data_size = MAX_REQUEST_DATA_SIZE

    def is_connected(self):
        return self.socket is not None

    def _generate_header(self, request_type, data_size):
        request_id = random.getrandbits(32)
        return (struct.pack("IIII", CURRENT_REQUEST_VERSION, request_id, request_type, data_size), request_id)

    def _read_and_validate_header(self, raw_reply, expected_id, expected_type):
        reply_version, reply_id, reply_type, reply_data_size = struct.unpack("IIII", raw_reply[:HEADER_SIZE])
        if (CURRENT_REQUEST_VERSION == reply_version and
            expected_id == reply_id and
            expected_type == reply_type and
            reply_data_size == len(raw_reply[HEADER_SIZE:])):
            return raw_reply[HEADER_SIZE:]
        return None

    def _request(self, request_type, request_data, timeout=None, resend=True):
        # Sends a request and returns the reply data, which is empty if the request failed.
        timeout = self.timeout if (timeout is None) else timeout
        attempts = self.retries + 1 if (timeout is not None and resend) else 1
        request, request_id = self._generate_header(request_type, len(request_data))
        self.socket.settimeout(timeout)
        for _ in range(attempts):
            self.socket.sendto(request + request_data, (self.address, self.port))
            try:
                while True:
                    raw_reply = self.socket.recv(MAX_PACKET_SIZE)
                    reply_data = self._read_and_validate_header(raw_reply, request_id, request_type)
                    if reply_data is not None:
                        return reply_data
            except socket.timeout:
                continue
        raise TimeoutError("No reply from the server after {} attempts".format(attempts))

    def process_list(self):
        processes = {}
        read_processes = 0
        while True:
            reply_data = self._request(RequestType.ProcessList,
                                       struct.pack("II", read_processes, 0x7FFFFFFF))
            if not reply_data:
                break
            read_count = struct.unpack("I", reply_data[0:4])[0]
            reply_data = reply_data[4:]
            if read_count == 0:
                break
            read_processes += read_count
            for i in range(read_count):
                proc_data = reply_data[i * 0x14 : (i + 1) * 0x14]
                proc_id, title_id, proc_name = struct.unpack("<IQ8s", proc_data)
                proc_name = proc_name.rstrip(b"\x00").decode("ascii")
                processes[proc_id] = (title_id, proc_name)
        return processes

    def get_process(self):
        reply_data = self._request(RequestType.SetGetProcess, struct.pack("II", 0, 0))
        if reply_data:
            return struct.unpack("I", reply_data)[0]
        else:
            return None

    def set_process(self, process_id):
        self._request(RequestType.SetGetProcess, struct.pack("II", 1, process_id))

    def read_memory(self, read_address, read_size):
        """
        >>> c.read_memory(0x100000, 4)
        b'\\x07\\x00\\x00\\xeb'
        """
        result = bytearray()
        while read_size > 0:
            temp_read_size = min(read_size, self.max_data_size)
            reply_data = self._request(RequestType.ReadMemory,
                                       struct.pack("II", read_address, temp_read_size))
            if reply_data:
                result += reply_data
                read_size -= len(reply_data)
                read_address += len(reply_data)
            else:
                return None

        return bytes(result)

    def write_memory(self, write_address, write_contents):
        """
        >>> c.write_memory(0x100000, b"\\xff\\xff\\xff\\xff")
        True
        >>> c.read_memory(0x100000, 4)
        b'\\xff\\xff\\xff\\xff'
        >>> c.write_memory(0x100000, b"\\x07\\x00\\x00\\xeb")
        True
        >>> c.read_memory(0x100000, 4)
        b'\\x07\\x00\\x00\\xeb'
        """
        write_size = len(write_contents)
        while write_size > 0:
            temp_write_size = min(write_size, self.max_data_size - 8)
            request_data = struct.pack("II", write_address, temp_write_size)
            request_data += write_contents[:temp_write_size]
            reply_data = self._request(RequestType.WriteMemory, request_data)

            if None != reply_data:
                write_address += temp_write_size
                write_size -= temp_write_size
                write_contents = write_contents[temp_write_size:]
            else:
                return False
        return True

    def _take_screenshot(self, res_scale, flags):
        # Do not resend, as a missed reply would return busy, instead wait long enough
        # for the server to time out.
        reply_timeout = None if self.timeout is None else max(self.timeout, 30)
        reply_data = self._request(RequestType.TakeScreenshot, struct.pack("II", res_scale, flags),
                                   timeout=reply_timeout, resend=False)
        if not reply_data:
            return None

        result, width, height, data_size = struct.unpack("IIII", reply_data[:16])
        if result != ScreenshotResult.Success:
            raise RuntimeError("Screenshot failed: {}".format(ScreenshotResult(result).name))

        chunks = []
        received = 0
        while received < data_size:
            temp_read_size = min(data_size - received, self.max_data_size)
            reply_data = self._request(RequestType.ReadScreenshot,
                                       struct.pack("II", received, temp_read_size))
            if not reply_data:
                return None
            chunks.append(reply_data)
            received += len(reply_data)

        return width, height, b"".join(chunks)

    def screenshot(self, res_scale=0, secondary_window=False):
        """
        Captures the next rendered frame and returns it as a PNG.
        res_scale == 0 uses the current internal resolution.
        Raises RuntimeError on error.

        >>> c.screenshot()[:8]
        b'\\x89PNG\\r\\n\\x1a\\n'
        """
        flags = SCREENSHOT_FLAG_SECONDARY_WINDOW if secondary_window else 0
        result = self._take_screenshot(res_scale, flags)
        if result is None:
            raise RuntimeError("Screenshot request failed")
        return result[2]

    def screenshot_raw(self, res_scale=0, secondary_window=False):
        """
        Like screenshot(), but returns (width, height, data) with data holding
        the raw RGB888 pixels. Skips PNG encoding on the server side, which is
        is much faster than the extra data to transfer when connected locally.

        >>> width, height, data = c.screenshot_raw()
        >>> len(data) == width * height * 3
        True
        """
        flags = SCREENSHOT_FLAG_RAW_RGB
        if secondary_window:
            flags |= SCREENSHOT_FLAG_SECONDARY_WINDOW
        return self._take_screenshot(res_scale, flags)

    def save_screenshot(self, path, res_scale=0, secondary_window=False):
        """
        Captures the next rendered frame and saves it as a PNG file at path.
        Raises RuntimeError on error.

        >>> import os, tempfile
        >>> tmp = tempfile.TemporaryDirectory()
        >>> path = os.path.join(tmp.name, "screenshot.png")
        >>> c.save_screenshot(path)
        True
        >>> with open(path, "rb") as f:
        ...     f.read(8)
        b'\\x89PNG\\r\\n\\x1a\\n'
        >>> tmp.cleanup()
        """
        png = self.screenshot(res_scale, secondary_window)
        if png is None:
            return False
        with open(path, "wb") as f:
            f.write(png)
        return True

    def perf_stats(self, reset=False):
        """
        Returns the emulator performance statistics as a dict, or None if not available.
        By default returns the stats last computed by the frontend (Qt and Android refresh
        them every few seconds). With reset=True, computes the stats since the previous
        reset and starts a new interval (makes the frontend's own display skip that interval).
        
        >>> sorted(c.perf_stats().keys()) == sorted(PERF_STATS_FIELDS)
        True
        """
        reply_data = self._request(RequestType.GetPerfStats, struct.pack("II", 1 if reset else 0, 0))
        if len(reply_data) < 0x54:
            return None
        return dict(zip(PERF_STATS_FIELDS, struct.unpack("<10dI", reply_data[:0x54])))

if "__main__" == __name__:
    import doctest
    doctest.testmod(extraglobs={'c': AzaharRPC()})
