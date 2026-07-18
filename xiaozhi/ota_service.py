"""Firmware publication and OTA manifest service for Xiaozhi devices."""

from __future__ import annotations

import hashlib
import hmac
import json
import os
import re
import tempfile
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ESP_IMAGE_MAGIC = 0xE9
ESP_APP_DESC_MAGIC = 0xABCD5432
ESP_APP_DESC_OFFSET = 32
ESP_APP_VERSION_OFFSET = 48
ESP_APP_PROJECT_OFFSET = 80
ESP_APP_FIELD_SIZE = 32
MAX_FIRMWARE_SIZE = 6 * 1024 * 1024
VERSION_RE = re.compile(r"^v?(\d+(?:\.\d+)*)$")
SAFE_NAME_RE = re.compile(r"[^a-zA-Z0-9._-]+")


class OtaError(ValueError):
    """A safe validation error that may be returned to the admin client."""


@dataclass(frozen=True)
class EspImageInfo:
    project: str
    version: str


def parse_version(value: str) -> tuple[int, ...]:
    """Parse a numeric dotted version without ever throwing stdlib internals."""
    match = VERSION_RE.fullmatch((value or "").strip())
    if not match:
        raise OtaError(f"invalid version: {value!r}")
    return tuple(int(part) for part in match.group(1).split("."))


def compare_versions(left: str, right: str) -> int:
    """Return -1, 0, or 1 after padding shorter numeric versions with zeros."""
    left_parts = parse_version(left)
    right_parts = parse_version(right)
    size = max(len(left_parts), len(right_parts))
    padded_left = left_parts + (0,) * (size - len(left_parts))
    padded_right = right_parts + (0,) * (size - len(right_parts))
    return (padded_left > padded_right) - (padded_left < padded_right)


def _read_c_string(data: bytes, offset: int, size: int) -> str:
    raw = data[offset:offset + size].split(b"\0", 1)[0]
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise OtaError("firmware metadata is not valid UTF-8") from exc


def inspect_esp_image(image: bytes) -> EspImageInfo:
    """Read esp_app_desc_t from the first app image segment."""
    minimum = ESP_APP_PROJECT_OFFSET + ESP_APP_FIELD_SIZE
    if len(image) < minimum:
        raise OtaError("firmware image is too small")
    if image[0] != ESP_IMAGE_MAGIC:
        raise OtaError("not an ESP app image (invalid image magic)")
    app_magic = int.from_bytes(
        image[ESP_APP_DESC_OFFSET:ESP_APP_DESC_OFFSET + 4], "little"
    )
    if app_magic != ESP_APP_DESC_MAGIC:
        raise OtaError("not an ESP app image (invalid app descriptor)")

    version = _read_c_string(image, ESP_APP_VERSION_OFFSET, ESP_APP_FIELD_SIZE)
    project = _read_c_string(image, ESP_APP_PROJECT_OFFSET, ESP_APP_FIELD_SIZE)
    if not version or not project:
        raise OtaError("firmware project or version is empty")
    parse_version(version)
    return EspImageInfo(project=project, version=version)


@dataclass
class FirmwareManifest:
    schema_version: int
    board: str
    project: str
    version: str
    filename: str
    size: int
    sha256: str
    force: bool
    rollout: int
    min_version: str
    release_notes: str
    published_at: str


class OtaService:
    def __init__(
        self,
        storage_dir: str | os.PathLike[str],
        public_base_url: str,
        websocket_url: str,
        admin_token: str,
        expected_project: str = "xiaozhi",
    ):
        self.storage_dir = Path(storage_dir).resolve()
        self.public_base_url = public_base_url.rstrip("/")
        self.websocket_url = websocket_url
        self.admin_token = admin_token
        self.expected_project = expected_project
        self.storage_dir.mkdir(parents=True, exist_ok=True)
        self.manifest_path = self.storage_dir / "manifest.json"

    def is_authorized(self, supplied_token: str) -> bool:
        return bool(self.admin_token) and hmac.compare_digest(
            self.admin_token, supplied_token or ""
        )

    def load_manifest(self) -> FirmwareManifest | None:
        if not self.manifest_path.exists():
            return None
        try:
            data = json.loads(self.manifest_path.read_text(encoding="utf-8"))
            return FirmwareManifest(**data)
        except (OSError, TypeError, ValueError, json.JSONDecodeError) as exc:
            raise OtaError(f"stored OTA manifest is invalid: {exc}") from exc

    def publish(
        self,
        image: bytes,
        *,
        version: str,
        board: str,
        force: bool = False,
        rollout: int = 100,
        min_version: str = "",
        release_notes: str = "",
    ) -> FirmwareManifest:
        if not image:
            raise OtaError("firmware image is empty")
        if len(image) > MAX_FIRMWARE_SIZE:
            raise OtaError(
                f"firmware exceeds {MAX_FIRMWARE_SIZE // (1024 * 1024)} MiB limit"
            )

        requested_version = (version or "").strip()
        parse_version(requested_version)
        info = inspect_esp_image(image)
        if info.project != self.expected_project:
            raise OtaError(
                f"wrong firmware project: expected {self.expected_project}, got {info.project}"
            )
        if compare_versions(info.version, requested_version) != 0:
            raise OtaError(
                f"version mismatch: image is {info.version}, form says {requested_version}"
            )

        board = (board or "").strip()
        if not board:
            raise OtaError("board is required")
        safe_board = SAFE_NAME_RE.sub("-", board).strip("-._")
        if not safe_board:
            raise OtaError("board contains no usable characters")
        rollout = int(rollout)
        if not 0 <= rollout <= 100:
            raise OtaError("rollout must be between 0 and 100")
        min_version = (min_version or "").strip()
        if min_version:
            parse_version(min_version)

        digest = hashlib.sha256(image).hexdigest()
        filename = f"{safe_board}-{requested_version}-{digest[:12]}.bin"
        target = self.storage_dir / filename
        self._atomic_write_bytes(target, image)

        manifest = FirmwareManifest(
            schema_version=1,
            board=board,
            project=info.project,
            version=requested_version,
            filename=filename,
            size=len(image),
            sha256=digest,
            force=bool(force),
            rollout=rollout,
            min_version=min_version,
            release_notes=(release_notes or "").strip()[:2000],
            published_at=datetime.now(timezone.utc).isoformat(),
        )
        self._atomic_write_bytes(
            self.manifest_path,
            json.dumps(asdict(manifest), ensure_ascii=False, indent=2).encode("utf-8"),
        )
        return manifest

    def get_firmware_path(self, filename: str) -> Path | None:
        manifest = self.load_manifest()
        if manifest is None or filename != manifest.filename:
            return None
        path = (self.storage_dir / filename).resolve()
        if path.parent != self.storage_dir or not path.is_file():
            return None
        return path

    def check(
        self, *, device_id: str, current_version: str, board: str
    ) -> dict[str, Any]:
        response: dict[str, Any] = {
            "server_time": {"timestamp": int(time.time() * 1000)},
            "websocket": {
                "url": self.websocket_url,
                "token": "",
                "version": 3,
            },
        }
        manifest = self.load_manifest()
        if manifest is None:
            return response

        try:
            comparison = compare_versions(current_version, manifest.version)
            version_is_newer = comparison < 0
            version_is_different = comparison != 0
        except OtaError:
            version_is_newer = False
            version_is_different = False

        board_matches = not board or board == manifest.board
        rollout_matches = self._in_rollout(
            device_id or "anonymous", manifest.version, manifest.rollout
        )
        below_minimum = False
        if manifest.min_version:
            try:
                below_minimum = compare_versions(
                    current_version, manifest.min_version
                ) < 0
            except OtaError:
                below_minimum = False

        if board_matches and (
            version_is_newer or (manifest.force and version_is_different)
        ):
            if rollout_matches or manifest.force or below_minimum:
                response["firmware"] = {
                    "version": manifest.version,
                    "url": (
                        f"{self.public_base_url}/ota/firmware/{manifest.filename}"
                    ),
                    "size": manifest.size,
                    "sha256": manifest.sha256,
                    "force": 1 if (manifest.force or below_minimum) else 0,
                    "release_notes": manifest.release_notes,
                }
        return response

    def status(self) -> dict[str, Any]:
        manifest = self.load_manifest()
        return {
            "configured": manifest is not None,
            "public_base_url": self.public_base_url,
            "websocket_url": self.websocket_url,
            "manifest": asdict(manifest) if manifest else None,
        }

    @staticmethod
    def _in_rollout(device_id: str, version: str, rollout: int) -> bool:
        if rollout >= 100:
            return True
        if rollout <= 0:
            return False
        digest = hashlib.sha256(f"{version}:{device_id}".encode("utf-8")).digest()
        bucket = int.from_bytes(digest[:4], "big") % 100
        return bucket < rollout

    @staticmethod
    def _atomic_write_bytes(path: Path, data: bytes) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
        try:
            with os.fdopen(fd, "wb") as handle:
                handle.write(data)
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(temporary, path)
        except Exception:
            try:
                os.unlink(temporary)
            except OSError:
                pass
            raise
