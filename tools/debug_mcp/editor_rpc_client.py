"""Thin Python client for the EpicMapEditor RPC server (raw TCP + line-JSON).

The editor listens on 127.0.0.1:9876 by default. Each request is one JSON
object terminated by '\\n'; each response is one JSON object terminated by '\\n'.

Usage:
    from tools.debug_mcp.editor_rpc_client import EditorRpcClient
    c = EditorRpcClient()
    print(c.call(op="ping"))
    print(c.list_chapters())
"""

from __future__ import annotations

import json
import socket
from typing import Any


class EditorRpcError(RuntimeError):
    """Raised when the server returns ok=false."""

    def __init__(self, kind: str, message: str):
        super().__init__(f"[{kind}] {message}")
        self.kind = kind
        self.message = message


class EditorRpcClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 9876, timeout: float = 10.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self._buf = b""

    # --- low-level ------------------------------------------------------

    def call(self, **kw: Any) -> dict:
        """Send one command and return the parsed JSON response.

        Raises EditorRpcError on ok=false, ConnectionError on transport issues.
        """
        line = (json.dumps(kw) + "\n").encode("utf-8")
        self.sock.sendall(line)
        data = self._readline()
        resp = json.loads(data.decode("utf-8"))
        if not resp.get("ok"):
            err = resp.get("error") or {}
            raise EditorRpcError(err.get("kind", "?"), err.get("message", ""))
        return resp

    def _readline(self) -> bytes:
        # TCP does not guarantee that one recv == one line; accumulate.
        while b"\n" not in self._buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("editor RPC server closed the connection")
            self._buf += chunk
        idx = self._buf.index(b"\n")
        line = self._buf[:idx]
        self._buf = self._buf[idx + 1:]
        return line

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    # --- high-level helpers --------------------------------------------

    def ping(self) -> bool:
        return bool(self.call(op="ping").get("data", {}).get("pong"))

    def status(self) -> dict:
        return self.call(op="status").get("data", {})

    def list_chapters(self) -> list[dict]:
        return self.call(op="list_chapters").get("data", {}).get("chapters", [])

    def load_chapter(self, name: str) -> dict:
        return self.call(op="load_chapter", args={"name": name}).get("data", {})

    def list_assets(self) -> list[dict]:
        return self.call(op="list_assets").get("data", {}).get("assets", [])

    def select_asset(self, uuid: str) -> dict:
        return self.call(op="select_asset", args={"uuid": uuid}).get("data", {})

    def select_tool(self, tool: str) -> dict:
        return self.call(op="select_tool", args={"tool": tool}).get("data", {})

    def click(self, x: int, y: int, ctrl: bool = False, shift: bool = False, alt: bool = False) -> dict:
        return self.call(op="click", args={"x": x, "y": y, "ctrl": ctrl, "shift": shift, "alt": alt}).get("data", {})

    def save(self) -> dict:
        return self.call(op="save").get("data", {})

    def reload(self) -> dict:
        return self.call(op="reload").get("data", {})

    # --- convenience: wait for map to be loaded after load_chapter ------

    def wait_until_map_loaded(self, timeout_s: float = 8.0, poll_interval: float = 0.2) -> bool:
        import time
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            st = self.status()
            if st.get("map_loaded"):
                return True
            time.sleep(poll_interval)
        return False


if __name__ == "__main__":
    # Quick smoke when run directly: ping + status.
    c = EditorRpcClient()
    print("ping:", c.ping())
    print("status:", c.status())
    c.close()
