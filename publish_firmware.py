#!/usr/bin/env python3
"""Publish a built Xiaozhi app image to the OTA admin endpoint."""

from __future__ import annotations

import argparse
import json
import urllib.parse
import urllib.request
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware", type=Path)
    parser.add_argument("--server", required=True, help="Admin base URL")
    parser.add_argument("--token", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--board", required=True)
    parser.add_argument("--rollout", type=int, default=100)
    parser.add_argument("--min-version", default="")
    parser.add_argument("--notes", default="")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    query = urllib.parse.urlencode(
        {
            "version": args.version,
            "board": args.board,
            "rollout": args.rollout,
            "min_version": args.min_version,
            "release_notes": args.notes,
            "force": "1" if args.force else "0",
        }
    )
    request = urllib.request.Request(
        f"{args.server.rstrip('/')}/api/ota/firmware?{query}",
        data=args.firmware.read_bytes(),
        method="POST",
        headers={
            "Content-Type": "application/octet-stream",
            "X-OTA-Token": args.token,
        },
    )
    with urllib.request.urlopen(request, timeout=120) as response:
        print(json.dumps(json.load(response), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
