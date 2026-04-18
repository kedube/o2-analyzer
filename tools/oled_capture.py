#!/usr/bin/env python3

import argparse
import binascii
import pathlib
import struct
import sys
import time
import zlib


WIDTH = 128
HEIGHT = 64
FRAMEBUFFER_SIZE = WIDTH * HEIGHT // 8
FRAME_HEADER = b"OLED_FRAME 128 64\n"
FRAME_HEADER_CRLF = b"OLED_FRAME 128 64\r\n"
FRAME_PREFIX = b"OLED_FRAME"


def _extract_framed_buffer(raw: bytes) -> bytes | None:
    for header in (FRAME_HEADER, FRAME_HEADER_CRLF):
        stripped = raw.lstrip()
        if stripped.startswith(header):
            frame = stripped[len(header):]
            if len(frame) >= FRAMEBUFFER_SIZE:
                return frame[:FRAMEBUFFER_SIZE]

        header_index = raw.find(header)
        if header_index != -1:
            frame_start = header_index + len(header)
            frame_end = frame_start + FRAMEBUFFER_SIZE
            if len(raw) >= frame_end:
                return raw[frame_start:frame_end]

    return None


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Convert a raw 128x64 Adafruit SSD1306 framebuffer dump into a PNG. "
            "The input may be a binary file, ASCII hex text, or a serial capture using "
            "the frame header 'OLED_FRAME 128 64\\n' followed by 1024 raw bytes. "
            "When using --serial, the tool sends 's' by default to request a frame."
        )
    )
    parser.add_argument(
        "input",
        nargs="?",
        help="Input file path. Omit when using --serial.",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="oled-screenshot.png",
        help="Output PNG path.",
    )
    parser.add_argument(
        "--scale",
        type=int,
        default=4,
        help="Integer scaling factor for the output PNG.",
    )
    parser.add_argument(
        "--format",
        choices=["auto", "binary", "hex"],
        default="auto",
        help="Input format when reading from a file.",
    )
    parser.add_argument(
        "--serial",
        help="Serial port path to read one framed OLED dump from.",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=9600,
        help="Serial baud rate used with --serial.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=10.0,
        help="Serial read timeout in seconds.",
    )
    parser.add_argument(
        "--startup-delay",
        type=float,
        default=2.0,
        help="Seconds to wait after opening the serial port before requesting a frame.",
    )
    parser.add_argument(
        "--command",
        default="s",
        help="Command sent to the device before waiting for a serial framebuffer dump.",
    )
    return parser.parse_args()


def _read_input_bytes(args: argparse.Namespace) -> bytes:
    if args.serial:
        return _read_serial_frame(
            args.serial,
            args.baud,
            args.timeout,
            args.command,
            args.startup_delay,
        )

    if not args.input:
        raise ValueError("an input file is required unless --serial is used")

    input_path = pathlib.Path(args.input)
    raw = input_path.read_bytes()

    if args.format == "binary":
        return _normalize_binary_input(raw)
    if args.format == "hex":
        return _parse_hex_dump(raw)

    stripped = raw.lstrip()
    if stripped.startswith(FRAME_HEADER):
        return _normalize_binary_input(stripped)

    if len(raw) == FRAMEBUFFER_SIZE:
        return raw

    try:
        return _parse_hex_dump(raw)
    except ValueError:
        return _normalize_binary_input(raw)


def _normalize_binary_input(raw: bytes) -> bytes:
    framed = _extract_framed_buffer(raw)
    if framed is not None:
        return framed

    for header in (FRAME_HEADER, FRAME_HEADER_CRLF):
        header_index = raw.find(header)
        if header_index != -1:
            frame_start = header_index + len(header)
            raise ValueError(
                f"found OLED frame header but only received {len(raw) - frame_start} framebuffer bytes"
            )

    if len(raw) == FRAMEBUFFER_SIZE:
        return raw

    raise ValueError(
        f"expected {FRAMEBUFFER_SIZE} bytes of framebuffer data, got {len(raw)}"
    )


def _parse_hex_dump(raw: bytes) -> bytes:
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ValueError("input is not ASCII hex text") from exc

    cleaned = "".join(ch for ch in text if ch in "0123456789abcdefABCDEF")
    if len(cleaned) != FRAMEBUFFER_SIZE * 2:
        raise ValueError(
            f"expected {FRAMEBUFFER_SIZE * 2} hex characters, got {len(cleaned)}"
        )

    try:
        return binascii.unhexlify(cleaned)
    except binascii.Error as exc:
        raise ValueError("invalid hex dump") from exc


def _read_serial_frame(
    port: str,
    baud: int,
    timeout: float,
    command: str,
    startup_delay: float,
) -> bytes:
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError(
            "pyserial is required for --serial. Install it with 'python3 -m pip install pyserial'."
        ) from exc

    with serial.Serial(
        port,
        baudrate=baud,
        timeout=timeout,
        rtscts=False,
        dsrdtr=False,
    ) as ser:
        ser.dtr = False
        ser.rts = False

        if startup_delay > 0:
            time.sleep(startup_delay)

        ser.reset_input_buffer()

        deadline = time.monotonic() + timeout
        buffer = bytearray()
        next_command_time = time.monotonic()
        saw_serial_data = False
        while time.monotonic() < deadline:
            if command and not saw_serial_data and time.monotonic() >= next_command_time:
                ser.write(command.encode("ascii"))
                ser.flush()
                next_command_time = time.monotonic() + 1.0

            chunk = ser.read(ser.in_waiting or 1)
            if not chunk:
                continue

            saw_serial_data = True
            buffer.extend(chunk)

            framed = _extract_framed_buffer(bytes(buffer))
            if framed is not None:
                return framed

            for header in (FRAME_HEADER, FRAME_HEADER_CRLF):
                header_index = buffer.find(header)
                if header_index != -1:
                    frame_start = header_index + len(header)
                    frame = bytes(buffer[frame_start:])
                    remaining = FRAMEBUFFER_SIZE - len(frame)
                    if remaining > 0:
                        frame += ser.read(remaining)

                    if len(frame) != FRAMEBUFFER_SIZE:
                        raise ValueError(
                            f"expected {FRAMEBUFFER_SIZE} framebuffer bytes from serial input, got {len(frame)}"
                        )
                    return frame

            prefix_index = buffer.find(FRAME_PREFIX)
            if prefix_index != -1:
                if prefix_index > 0:
                    del buffer[:prefix_index]
                continue

            if len(buffer) > len(FRAME_HEADER) * 4:
                trailing_keep = len(FRAME_PREFIX) - 1
                del buffer[:-trailing_keep]

        header_preview = bytes(buffer[:80])
        if not header_preview:
            raise ValueError(
                "timed out waiting for OLED frame header; no serial data received"
            )
        raise ValueError(
            "unexpected serial data while waiting for OLED frame header: "
            f"{header_preview!r}"
        )


def _framebuffer_to_rows(framebuffer: bytes) -> list[bytes]:
    rows = []
    for y in range(HEIGHT):
        row = bytearray()
        row.append(0)
        for x in range(WIDTH):
            page = y // 8
            index = x + (page * WIDTH)
            bit = (framebuffer[index] >> (y % 8)) & 0x01
            pixel = 0 if bit else 255
            row.extend((pixel, pixel, pixel))
        rows.append(bytes(row))
    return rows


def _scale_rows(rows: list[bytes], scale: int) -> list[bytes]:
    if scale == 1:
        return rows

    scaled_rows = []
    for row in rows:
        expanded = bytearray()
        for i in range(1, len(row), 3):
            pixel = row[i:i + 3]
            expanded.extend(pixel * scale)
        scaled = bytes([0]) + bytes(expanded)
        for _ in range(scale):
            scaled_rows.append(scaled)
    return scaled_rows


def _png_chunk(chunk_type: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(chunk_type)
    crc = zlib.crc32(data, crc)
    return struct.pack(
        ">I", len(data)
    ) + chunk_type + data + struct.pack(
        ">I", crc & 0xFFFFFFFF
    )


def _write_png(path: pathlib.Path, framebuffer: bytes, scale: int) -> None:
    width = WIDTH * scale
    height = HEIGHT * scale
    rows = _scale_rows(_framebuffer_to_rows(framebuffer), scale)
    ihdr = struct.pack(
        ">IIBBBBB", width, height, 8, 2, 0, 0, 0
    )
    compressed = zlib.compress(b"".join(rows), level=9)
    png = b"".join(
        [
            b"\x89PNG\r\n\x1a\n",
            _png_chunk(b"IHDR", ihdr),
            _png_chunk(b"IDAT", compressed),
            _png_chunk(b"IEND", b""),
        ]
    )
    path.write_bytes(png)


def main() -> int:
    args = _parse_args()

    if args.scale < 1:
        print("scale must be >= 1", file=sys.stderr)
        return 2

    try:
        framebuffer = _read_input_bytes(args)
        output_path = pathlib.Path(args.output)
        _write_png(output_path, framebuffer, args.scale)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())