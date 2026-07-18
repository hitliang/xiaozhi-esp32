"""Opus encoder/decoder for 16kHz mono 60ms frames."""

import opuslib
from typing import List
from .protocol import (
    OPUS_SAMPLE_RATE, OPUS_CHANNELS, OPUS_SAMPLES,
    PLAYBACK_SAMPLE_RATE, PLAYBACK_OPUS_SAMPLES,
)

FRAME_BYTES = PLAYBACK_OPUS_SAMPLES * 2  # 1440 samples * 16-bit = 2880 bytes


class OpusDecoder:
    def __init__(self):
        self._dec = opuslib.Decoder(OPUS_SAMPLE_RATE, OPUS_CHANNELS)

    def decode(self, opus_data: bytes) -> bytes:
        """Decode an Opus frame to PCM bytes (16-bit little-endian).
        Returns empty bytes on decode failure (e.g. FEC/frame loss).
        """
        try:
            return self._dec.decode(opus_data, OPUS_SAMPLES)
        except opuslib.OpusError:
            return b""

    def decode_to_samples(self, opus_data: bytes) -> List[int]:
        """Decode to list of 16-bit samples."""
        pcm = self.decode(opus_data)
        if not pcm:
            return []
        return [
            int.from_bytes(pcm[i:i + 2], "little", signed=True)
            for i in range(0, len(pcm), 2)
        ]


class OpusEncoder:
    def __init__(self):
        self._enc = opuslib.Encoder(
            fs=PLAYBACK_SAMPLE_RATE,
            channels=OPUS_CHANNELS,
            application=opuslib.APPLICATION_AUDIO,
        )
        self._enc.bitrate = 48000
        self._pcm_buffer = bytearray()

    def reset_buffer(self):
        """Discard an unfinished PCM frame before starting a new utterance."""
        self._pcm_buffer.clear()

    def encode(self, pcm_bytes: bytes) -> bytes:
        """Encode one 24 kHz PCM16 mono frame (60 ms) to Opus."""
        if len(pcm_bytes) < FRAME_BYTES:
            pcm_bytes = pcm_bytes + b"\x00" * (FRAME_BYTES - len(pcm_bytes))
        return self._enc.encode(pcm_bytes[:FRAME_BYTES], PLAYBACK_OPUS_SAMPLES)

    def encode_pcm(self, pcm_bytes: bytes, flush: bool = False) -> List[bytes]:
        """Encode a continuous PCM stream into 60 ms Opus frames.

        Partial data is retained between calls. Silence padding is applied only
        once when ``flush`` is true at the end of the complete utterance.
        """
        if pcm_bytes:
            self._pcm_buffer.extend(pcm_bytes)

        frames = []
        while len(self._pcm_buffer) >= FRAME_BYTES:
            chunk = bytes(self._pcm_buffer[:FRAME_BYTES])
            del self._pcm_buffer[:FRAME_BYTES]
            frames.append(self.encode(chunk))

        if flush and self._pcm_buffer:
            frames.append(self.encode(bytes(self._pcm_buffer)))
            self._pcm_buffer.clear()

        return frames
