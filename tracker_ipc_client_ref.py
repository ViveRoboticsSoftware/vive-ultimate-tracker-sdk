#!/usr/bin/env python3
"""
tracker_ipc_client_ref.py - Python implementation reference of vive::ITrackerIPCClient

Mirrors the C++ IPC client in vivetracker/src/vive_tracker.cpp: connects to
the tracker server's Unix Domain Socket (default: /tmp/vive_tracker.sock),
polls for packets, and decodes them the same way Poll() does in the C++ SDK.

Wire protocol (see vivetracker/src/vive_tracker.cpp):
  TrackerIPCHeader (16 bytes, packed):
    uint32_t Id
    uint8_t  Msg        # TrackerMsg enum
    uint8_t  Payload    # number of payload bytes following the header
    uint8_t  Params[10]

  TrackerMsg:
    Stop=0, Start=1, Add=2, Update=3, Click=4, Remove=5, Status=6

  Update payload (>= 64 bytes), little-endian floats/ints:
    float Location[3];
    float Rotation[4];  # qx, qy, qz, qw
    float Velocity[3];
    float AngularVelocity[3];
    int64_t Timestamp_us;
    uint32_t Flags;
  Params for Update: [0]=State [1]=Button [2]=Clicks [3]=Battery

  Add payload: optional tracker name (not null-terminated on the wire).
  Click: Params[0]=button_id, Params[1]=clicks. Id=tracker_id.
  Remove/Start/Stop: no payload.
  Status (command response): Params[0:NUM_TOTAL_TRACKERS]=TrackerState per
  tracker, payload=command string, Id=result code (0=Failed,1=Ignore,else OK).

Commands (ITrackerSubscriber::SendCommand / ITrackerIPCClient::SendCommand):
  a case-insensitive c-string + optional 0-based id list, sent as a fixed
  64-byte, zero-padded buffer, e.g. "Pairing 0", "Unpair 0,1,2", "Restart all",
  "PowerOff all", "Echo", "Setup".

Usage:
  python tracker_ipc_client_ref.py [--socket /tmp/vive_tracker.sock]

Note: this uses AF_UNIX, so it must run on the same Linux host as the
tracker server (or inside WSL / an SSH session into it) - it will not work
natively on Windows against a plain Windows Python interpreter.
"""

import argparse
import socket
import struct
import sys
import time

NUM_TOTAL_TRACKERS = 5  # NUM_DONGLE_TRACKERS in vive_tracker.cpp

# TrackerMsg enum
MSG_STOP = 0
MSG_START = 1
MSG_ADD = 2
MSG_UPDATE = 3
MSG_CLICK = 4
MSG_REMOVE = 5
MSG_STATUS = 6

MSG_NAMES = {
    MSG_STOP: "Stop", MSG_START: "Start", MSG_ADD: "Add",
    MSG_UPDATE: "Update", MSG_CLICK: "Click", MSG_REMOVE: "Remove",
    MSG_STATUS: "Status",
}

# TrackerState enum
TRACKER_STATE_NAMES = {
    0: "Unpaired", 1: "Pairing", 2: "Paired", 3: "Connecting",
    4: "Connected", 5: "Initialized", 6: "Syncing", 7: "BuildMap", 8: "Ready",
}

HEADER_FMT = "<IBB10s"          # Id, Msg, Payload, Params[10]
HEADER_SIZE = struct.calcsize(HEADER_FMT)
assert HEADER_SIZE == 16

UPDATE_FMT = "<3f4f3f3fqI"      # Location3, Rotation4, Velocity3, AngularVelocity3, Timestamp_us, Flags
UPDATE_SIZE = struct.calcsize(UPDATE_FMT)
assert UPDATE_SIZE == 64

COMMAND_BUF_SIZE = 64


def command_result_str(code: int) -> str:
    if code == 0:
        return "Failed"
    if code == 1:
        return "Ignore"
    return "OK"


class TrackerIPCClient:
    """Python port of vive::ITrackerIPCClient. Subclass and override the
    on_* callbacks, then call poll() repeatedly (e.g. once per frame)."""

    def __init__(self, socket_path="/tmp/vive_tracker.sock"):
        self.socket_path = socket_path
        self.sock = None

    # -- connection management -------------------------------------------------
    def connect(self) -> bool:
        if self.sock is not None:
            return True
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(self.socket_path)
            s.setblocking(False)
            self.sock = s
            return True
        except OSError:
            self.sock = None
            return False

    def disconnect(self):
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    def _recv_exact(self, n: int) -> bytes:
        # payload bytes are expected to arrive right after the header, so we
        # briefly switch to blocking mode to read them fully (mirrors
        # MSG_WAITALL used in the C++ client).
        self.sock.setblocking(True)
        try:
            chunks = bytearray()
            while len(chunks) < n:
                chunk = self.sock.recv(n - len(chunks))
                if not chunk:
                    raise ConnectionError("server closed connection")
                chunks += chunk
            return bytes(chunks)
        finally:
            self.sock.setblocking(False)

    # -- polling -----------------------------------------------------------
    def poll(self) -> bool:
        """Call every frame. Returns False if not connected."""
        if self.sock is None:
            self.connect()
            return self.sock is not None

        try:
            while True:
                try:
                    hdr_bytes = self.sock.recv(HEADER_SIZE)
                except BlockingIOError:
                    break  # nothing to read right now
                if not hdr_bytes:
                    raise ConnectionError("server closed connection")
                if len(hdr_bytes) < HEADER_SIZE:
                    hdr_bytes += self._recv_exact(HEADER_SIZE - len(hdr_bytes))

                Id, Msg, Payload, Params = struct.unpack(HEADER_FMT, hdr_bytes)
                payload_bytes = self._recv_exact(Payload) if Payload > 0 else b""
                self._dispatch(Id, Msg, Payload, Params, payload_bytes)
        except (ConnectionError, OSError) as e:
            print(f"[TrackerIPCClient] server disconnected: {e}", file=sys.stderr)
            self.disconnect()
            return False

        return True

    def _dispatch(self, Id, Msg, Payload, Params, payload_bytes):
        if Msg == MSG_ADD:
            name = payload_bytes.decode("utf-8", "replace") if Payload > 0 else None
            self.on_add(Id, name)
        elif Msg == MSG_UPDATE:
            if Payload >= UPDATE_SIZE:
                vals = struct.unpack(UPDATE_FMT, payload_bytes[:UPDATE_SIZE])
                data = {
                    "Id": Id,
                    "Location": vals[0:3],
                    "Rotation": vals[3:7],       # qx, qy, qz, qw
                    "Velocity": vals[7:10],
                    "AngularVelocity": vals[10:13],
                    "Timestamp_us": vals[13],
                    "Flags": vals[14],
                    "State": Params[0],
                    "Button": Params[1],
                    "Clicks": Params[2],
                    "Battery": Params[3],
                }
                self.on_update(data)
        elif Msg == MSG_CLICK:
            self.on_click(Id, Params[0], Params[1])
        elif Msg == MSG_REMOVE:
            self.on_remove(Id)
        elif Msg == MSG_START:
            self.on_server_start()
        elif Msg == MSG_STOP:
            self.on_server_stop()
        elif Msg == MSG_STATUS:
            cmd = payload_bytes.split(b"\0", 1)[0].decode("utf-8", "replace")
            states = list(Params[:NUM_TOTAL_TRACKERS])
            result = command_result_str(Id)
            self.on_command_response(cmd, result, states)
        else:
            print(f"[TrackerIPCClient] unknown msg type: {Msg}", file=sys.stderr)

    # -- commands ------------------------------------------------------------
    def send_command(self, cmd: str) -> bool:
        if self.sock is None:
            return False
        buf = cmd.encode("utf-8")[:COMMAND_BUF_SIZE]
        buf = buf + b"\0" * (COMMAND_BUF_SIZE - len(buf))
        try:
            self.sock.setblocking(True)
            self.sock.sendall(buf)
            return True
        except OSError as e:
            print(f"[TrackerIPCClient] send command failed: {e}", file=sys.stderr)
            self.disconnect()
            return False
        finally:
            if self.sock is not None:
                self.sock.setblocking(False)

    # -- callbacks (override in a subclass) ----------------------------------
    def on_server_start(self):
        print("[ServerStart]")

    def on_server_stop(self):
        print("[ServerStop]")

    def on_add(self, tracker_id, name):
        print(f"[Add] id={tracker_id} name={name!r}")

    def on_update(self, data):
        state = TRACKER_STATE_NAMES.get(data["State"], data["State"])
        print(
            f"[Update] id={data['Id']} state={state} "
            f"loc={data['Location']} rot={data['Rotation']} "
            f"battery={data['Battery']}%"
        )

    def on_click(self, tracker_id, button_id, clicks):
        print(f"[Click] id={tracker_id} button={button_id} clicks={clicks}")

    def on_remove(self, tracker_id):
        print(f"[Remove] id={tracker_id}")

    def on_command_response(self, cmd, result, states):
        print(f"[CommandResponse] cmd={cmd!r} result={result} states={states}")


def main():
    parser = argparse.ArgumentParser(description="Poll the VIVE tracker IPC server")
    parser.add_argument("--socket", default="/tmp/vive_tracker.sock",
                         help="Unix domain socket path (default: %(default)s)")
    parser.add_argument("--rate", type=float, default=60.0,
                         help="poll rate in Hz (default: %(default)s)")
    args = parser.parse_args()

    client = TrackerIPCClient(args.socket)
    interval = 1.0 / args.rate if args.rate > 0 else 0
    was_connected = False

    print(f"Connecting to {args.socket} ...")
    try:
        while True:
            connected = client.poll()
            if connected and not was_connected:
                print("Connected.")
            elif not connected and was_connected:
                print("Disconnected, retrying...")
            was_connected = connected
            time.sleep(interval if connected else 0.5)
    except KeyboardInterrupt:
        pass
    finally:
        client.disconnect()


if __name__ == "__main__":
    main()
