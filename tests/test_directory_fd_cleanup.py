#!/usr/bin/env python3
"""Regression test for leaking legacy OPENDIR descriptors on Linux."""

import os
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path


HOST = "127.0.0.1"
ANY_ADDRESS = "0.0.0.0"
OPEN_CLOSE_CYCLES = 64
MAX_DIRECTORY_HANDLES = 8

TNFS_MOUNT = 0x00
TNFS_UMOUNT = 0x01
TNFS_OPENDIR = 0x10
TNFS_CLOSEDIR = 0x12
TNFS_SUCCESS = 0x00


def find_available_port():
    """Find a port currently available for both TCP and UDP."""
    for _ in range(20):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
            tcp_socket.bind((ANY_ADDRESS, 0))
            port = tcp_socket.getsockname()[1]

            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_socket:
                    udp_socket.bind((ANY_ADDRESS, port))
            except OSError:
                continue

            return port

    raise RuntimeError("could not find a port available for both TCP and UDP")


def request(client, address, session_id, sequence_number, command, payload=b""):
    packet = struct.pack("<HBB", session_id, sequence_number, command) + payload
    client.sendto(packet, address)
    response, _ = client.recvfrom(532)

    if len(response) < 5:
        raise AssertionError(f"short response for command 0x{command:02x}: {response!r}")
    if response[4] != TNFS_SUCCESS:
        raise AssertionError(
            f"command 0x{command:02x} failed with status 0x{response[4]:02x}"
        )

    return response


def wait_until_ready(server, client, address):
    deadline = time.monotonic() + 5

    while time.monotonic() < deadline:
        if server.poll() is not None:
            raise RuntimeError(f"tnfsd exited during startup with status {server.returncode}")

        try:
            client.settimeout(0.1)
            response = request(
                client,
                address,
                0,
                1,
                TNFS_MOUNT,
                b"\x03\x01\x00\x00\x00",
            )
            return struct.unpack_from("<H", response)[0]
        except (socket.timeout, OSError):
            time.sleep(0.05)

    raise TimeoutError("tnfsd did not begin responding to UDP requests")


def descriptor_count(server):
    return len(os.listdir(f"/proc/{server.pid}/fd"))


def wait_for_descriptor_count(server, expected):
    """Wait for cleanup that occurs immediately after the response is sent."""
    deadline = time.monotonic() + 1
    count = descriptor_count(server)
    while count != expected and time.monotonic() < deadline:
        time.sleep(0.01)
        count = descriptor_count(server)
    return count


def stop_server(server):
    if server.poll() is not None:
        return

    server.send_signal(signal.SIGINT)
    try:
        server.wait(timeout=3)
    except subprocess.TimeoutExpired:
        server.kill()
        server.wait(timeout=3)


def main():
    if not sys.platform.startswith("linux"):
        print("SKIP: descriptor inspection requires Linux /proc")
        return

    default_server = Path(__file__).resolve().parents[1] / "bin" / "tnfsd"
    server_path = Path(sys.argv[1] if len(sys.argv) > 1 else default_server).resolve()
    if not server_path.is_file():
        raise FileNotFoundError(f"tnfsd binary not found: {server_path}")

    port = find_available_port()
    address = (HOST, port)

    with tempfile.TemporaryDirectory(prefix="tnfsd-directory-fd-") as root_dir:
        server = subprocess.Popen(
            (str(server_path), "-p", str(port), root_dir),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
                client.settimeout(2)
                session_id = wait_until_ready(server, client, address)
                baseline = descriptor_count(server)
                sequence_number = 2

                for _ in range(OPEN_CLOSE_CYCLES):
                    opened = request(
                        client,
                        address,
                        session_id,
                        sequence_number,
                        TNFS_OPENDIR,
                        b"/\x00",
                    )
                    sequence_number = (sequence_number + 1) & 0xFF
                    request(
                        client,
                        address,
                        session_id,
                        sequence_number,
                        TNFS_CLOSEDIR,
                        opened[5:6],
                    )
                    sequence_number = (sequence_number + 1) & 0xFF

                after_cycles = descriptor_count(server)
                if after_cycles > baseline + MAX_DIRECTORY_HANDLES:
                    raise AssertionError(
                        "directory descriptors grew without bound: "
                        f"baseline={baseline}, after_cycles={after_cycles}"
                    )

                request(
                    client,
                    address,
                    session_id,
                    sequence_number,
                    TNFS_UMOUNT,
                )
                after_unmount = wait_for_descriptor_count(server, baseline)
                if after_unmount != baseline:
                    raise AssertionError(
                        "directory descriptors remained after unmount: "
                        f"baseline={baseline}, after_unmount={after_unmount}"
                    )
        finally:
            stop_server(server)

    print(
        "PASS: directory descriptors stayed bounded and returned to baseline "
        f"after {OPEN_CLOSE_CYCLES} OPENDIR/CLOSEDIR cycles"
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
