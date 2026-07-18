"""Xiaozhi WebSocket server for Reddy voice assistant.

Implements the xiaozhi binary protocol v3 to communicate with ESP32-S3 devices.
Flow: hello handshake -> receive Opus audio -> VAD -> STT -> LLM -> TTS -> send Opus audio
"""

from __future__ import annotations

import asyncio
import json
import logging
import struct
import wave
import io
import time
from collections import deque

import websockets
from websockets.server import WebSocketServerProtocol

from .config import load_config, Config
from .protocol import (
    BIN_TYPE_OPUS, BIN_TYPE_JSON, HEADER_SIZE,
    OPUS_FRAME_MS,
    pack_binary_frame, pack_opus_frame, pack_json_frame,
    make_hello_response, make_stt, make_llm_emotion, make_tts_event,
)
from .codec import OpusDecoder, OpusEncoder
from .stt import AliyunSTT
from .tts import TTSService
from .agent import AgentEngine, LLMClient, ToolExecutor, parse_spoken_reply
from .memory import ConversationMemory
from .admin import AdminServer

logger = logging.getLogger("xz.server")

# ---- Tool handlers ----
import math as _math

_SAFE_MATH = {
    "sqrt": _math.sqrt, "pow": _math.pow,
    "sin": _math.sin, "cos": _math.cos, "tan": _math.tan,
    "pi": _math.pi, "e": _math.e,
    "floor": _math.floor, "ceil": _math.ceil,
    "log": _math.log, "log10": _math.log10, "log2": _math.log2,
}
_SAFE_BUILTINS = {"abs": abs, "round": round, "min": min, "max": max, "int": int, "float": float}
_SAFE_NAMES = {**_SAFE_BUILTINS, **_SAFE_MATH}


def _safe_eval(expr: str) -> str:
    expr = expr.strip().replace("^", "**")
    allowed = set("0123456789.+-*/%() _abcdefghijklmnopqrstuvwxyz")
    for ch in expr:
        if ch.lower() not in allowed:
            return f"Unsupported char: {ch}"
    try:
        compiled = compile(expr, "<calc>", "eval")
        for name in compiled.co_names:
            if name not in _SAFE_NAMES:
                return f"Unsupported function: {name}"
        result = eval(compiled, {"__builtins__": {}}, _SAFE_NAMES)
        if isinstance(result, float):
            if abs(result - round(result)) < 1e-10:
                return str(int(round(result)))
            return f"{result:.6f}".rstrip("0").rstrip(".")
        return str(result)
    except ZeroDivisionError:
        return "Cannot divide by zero"
    except Exception as e:
        return f"Calculation error: {e}"


async def _calc_handler(expression: str) -> str:
    return f"Result: {_safe_eval(expression)}"


# VAD settings
VAD_ENERGY_THRESHOLD = 500
VAD_SILENCE_FRAMES = 15
VAD_MIN_SPEECH_FRAMES = 3
VAD_MAX_SPEECH_FRAMES = 500

# Audio resampling
class DeviceSession:
    """Per-device session state."""

    def __init__(self, ws: WebSocketServerProtocol):
        self.ws = ws
        self.decoder = OpusDecoder()
        self.encoder = OpusEncoder()
        self.audio_buffer = bytearray()
        self.listening = False
        self.connected = False
        self.memory_started = False
        self.speaking = False
        self.vad_silence_count = 0
        self.vad_speech_frames = 0
        self.vad_active = False

    def reset_vad(self):
        self.audio_buffer = bytearray()
        self.vad_silence_count = 0
        self.vad_speech_frames = 0
        self.vad_active = False


class XiaozhiServer:
    def __init__(self, config: Config):
        self.config = config
        self.stt = AliyunSTT(
            config.stt.access_key_id,
            config.stt.access_key_secret,
            config.stt.appkey,
        )
        self.tts = TTSService(config.tts)
        self.memory = ConversationMemory(config.memory.db_path)

        self.llm = LLMClient(config.llm)
        self.tools = ToolExecutor()
        self._register_tools()

        # Load system prompt: DB > .env > default
        system_prompt = self._load_system_prompt(config.system_prompt)
        self.agent = AgentEngine(
            self.llm, self.memory, self.tools, system_prompt,
        )

        self._sessions: dict[str, DeviceSession] = {}

    def _load_system_prompt(self, fallback: str) -> str:
        """Load system prompt from DB, fall back to config/.env value."""
        import aiosqlite, os
        db_path = self.config.memory.db_path
        # Resolve relative path
        if db_path.startswith("./"):
            db_path = os.path.join(os.path.dirname(__file__), "..", db_path[2:])
        try:
            import asyncio
            async def _load():
                db = await aiosqlite.connect(db_path)
                try:
                    await db.execute(
                        "CREATE TABLE IF NOT EXISTS settings "
                        "(key TEXT PRIMARY KEY, value TEXT)"
                    )
                    await db.commit()
                    cursor = await db.execute(
                        "SELECT value FROM settings WHERE key='system_prompt'"
                    )
                    row = await cursor.fetchone()
                    if row and row[0]:
                        logger.info("Using system prompt from DB (admin page)")
                        return row[0]
                finally:
                    await db.close()
                return fallback or ""
            # We're in __init__ which is sync, but sqlite3 is fine
            import sqlite3
            db = sqlite3.connect(db_path)
            db.execute(
                "CREATE TABLE IF NOT EXISTS settings "
                "(key TEXT PRIMARY KEY, value TEXT)"
            )
            db.commit()
            cursor = db.execute(
                "SELECT value FROM settings WHERE key='system_prompt'"
            )
            row = cursor.fetchone()
            db.close()
            if row and row[0]:
                logger.info("Using system prompt from DB (admin page)")
                return row[0]
            if fallback:
                logger.info("Using system prompt from .env/SYSTEM_PROMPT")
                return fallback
        except Exception as e:
            logger.warning(f"Could not load prompt from DB: {e}")
        logger.info("Using default system prompt")
        return ""

    def reload_system_prompt(self):
        """Reload system prompt from DB (called after admin edit)."""
        self.agent.system_prompt = self._load_system_prompt(
            self.config.system_prompt
        )
        logger.info("System prompt reloaded")

    def _register_tools(self):
        self.tools.register(
            {
                "type": "function",
                "function": {
                    "name": "calculator",
                    "description": "Evaluate math expressions. Supports +-*/^ and sqrt/abs/round etc.",
                    "parameters": {
                        "type": "object",
                        "properties": {
                            "expression": {
                                "type": "string",
                                "description": "Math expression to evaluate, e.g. '3*17' or 'sqrt(81)'",
                            }
                        },
                        "required": ["expression"],
                    },
                },
            },
            _calc_handler,
        )

    async def start(self):
        await self.memory.load_or_create()

        self._admin = AdminServer(self.config.memory.db_path, self.config.server.host, 7071)
        admin_task = asyncio.create_task(self._admin.start())

        logger.info(f"Server starting on {self.config.server.host}:{self.config.server.port}")
        async with websockets.serve(
            self._handle_connection,
            self.config.server.host,
            self.config.server.port,
            max_size=2 * 1024 * 1024,
            # The embedded client has an interoperability issue with RFC ping
            # frames. Microphone traffic keeps the socket active; TCP detects loss.
            ping_interval=None,
            ping_timeout=None,
        ):
            await asyncio.Future()

        admin_task.cancel()

    async def _handle_connection(self, ws: WebSocketServerProtocol):
        peer = ws.remote_address
        logger.info(f"Client connected: {peer}")

        session = DeviceSession(ws)
        session_id = f"{peer[0]}:{peer[1]}"
        self._sessions[session_id] = session

        try:
            async for message in ws:
                if isinstance(message, bytes):
                    await self._handle_binary(session, message)
                else:
                    await self._handle_json(session, message)
        except websockets.exceptions.ConnectionClosed:
            logger.info(f"Client disconnected: {peer}")
        except Exception as e:
            logger.error(f"Session error {peer}: {e}")
        finally:
            self._sessions.pop(session_id, None)

    async def _handle_json(self, session: DeviceSession, text: str):
        try:
            msg = json.loads(text)
        except json.JSONDecodeError:
            return

        msg_type = msg.get("type", "")
        logger.info(f"<- JSON: {text[:200]}")

        if msg_type == "hello":
            await self._on_hello(session, msg)
        elif msg_type == "listen":
            await self._on_listen(session, msg)

    async def _handle_binary(self, session: DeviceSession, data: bytes):
        if len(data) < HEADER_SIZE:
            return

        frame_type = data[0]
        payload_size = (data[2] << 8) | data[3]
        payload = data[HEADER_SIZE:HEADER_SIZE + payload_size]

        if frame_type == BIN_TYPE_OPUS:
            await self._on_opus(session, payload)

    async def _on_hello(self, session: DeviceSession, msg: dict):
        if not session.memory_started:
            self.memory.start_session()
            session.memory_started = True
        resp = make_hello_response()
        await session.ws.send(json.dumps(resp, ensure_ascii=False))
        session.connected = True
        logger.info("Hello handshake complete")

    async def _on_listen(self, session: DeviceSession, msg: dict):
        state = msg.get("state", "")
        if state == "start":
            session.listening = True
            session.reset_vad()
            logger.info("Listening started")
        elif state == "stop":
            session.listening = False
            logger.info("Listening stopped")

    async def _on_opus(self, session: DeviceSession, opus_data: bytes):
        if not session.listening:
            return
        if session.speaking:
            return

        pcm = session.decoder.decode(opus_data)
        if not pcm:
            return

        session.audio_buffer.extend(pcm)

        energy = self._rms(pcm)
        is_speech = energy > VAD_ENERGY_THRESHOLD

        if is_speech:
            session.vad_silence_count = 0
            session.vad_speech_frames += 1

            if not session.vad_active and session.vad_speech_frames >= VAD_MIN_SPEECH_FRAMES:
                session.vad_active = True
        else:
            if session.vad_active:
                session.vad_silence_count += 1

        max_frames = VAD_MAX_SPEECH_FRAMES
        if session.vad_active and (
            session.vad_silence_count >= VAD_SILENCE_FRAMES
            or session.vad_speech_frames >= max_frames
        ):
            await self._process_speech(session)

    async def _process_speech(self, session: DeviceSession):
        """Process accumulated speech audio."""
        pcm = bytes(session.audio_buffer)
        session.reset_vad()
        session.listening = False

        logger.info(f"Processing speech: {len(pcm)} bytes ({len(pcm)/32000:.1f}s)")

        # STT
        text = await self.stt.transcribe(pcm)
        if not text:
            logger.info("STT returned empty, resuming listening")
            session.listening = True
            return

        # Send STT result to device
        await session.ws.send(json.dumps(make_stt(text), ensure_ascii=False))

        # Agent
        reply = await self.agent.process(text)
        logger.info(f"Agent reply: {reply}")

        if not reply:
            session.listening = True
            return

        # Language check: if LLM outputs Chinese, request English retry (up to 2 retries)
        max_retries = 2
        for retry in range(max_retries):
            if not self._contains_chinese(reply):
                break
            logger.warning(f"LLM reply contains Chinese (attempt {retry+1}), requesting English retry")
            english_reply = await self._force_english(text, reply)
            if english_reply and not self._contains_chinese(english_reply):
                reply = english_reply
                logger.info(f"English retry success: {reply}")
                break
            elif english_reply:
                reply = english_reply  # still has Chinese, but use the improved version
            else:
                break

        # The leading style tag is private control data. It must never appear in
        # subtitles or be pronounced by TTS.
        spoken_text, emotion = parse_spoken_reply(reply)
        if not spoken_text:
            spoken_text = "I hear you. Please say that once more."
            emotion = "confused"

        # TTS + the matching animated face
        await self._send_tts(session, spoken_text, emotion)

        # Resume listening
        session.listening = True

    async def _send_tts(self, session: DeviceSession, text: str, emotion: str = "happy"):
        """Stream native 24 kHz TTS without gaps or packet bursts."""
        session.speaking = True
        session.encoder.reset_buffer()

        # Send this first so the device saves the selected face before the
        # following TTS "start" switches its visual state to speaking.
        await session.ws.send(json.dumps(make_llm_emotion(emotion), ensure_ascii=False))
        await session.ws.send(json.dumps(make_tts_event("start", text), ensure_ascii=False))
        await session.ws.send(json.dumps(make_tts_event("sentence_start", text), ensure_ascii=False))

        first_frame = True
        total_pcm = 0
        frames_sent = 0
        pending_byte = b""
        last_frame_at = None
        frame_period = OPUS_FRAME_MS / 1000.0
        loop = asyncio.get_running_loop()

        async def send_frames(opus_frames):
            nonlocal first_frame, frames_sent, last_frame_at
            for frame in opus_frames:
                # Never burst packets faster than the device consumes its 60 ms
                # playback frames. Bursts overflow the small ESP32 decode queue.
                if last_frame_at is not None:
                    delay = frame_period - (loop.time() - last_frame_at)
                    if delay > 0:
                        await asyncio.sleep(delay)
                await session.ws.send(pack_opus_frame(frame))
                last_frame_at = loop.time()
                frames_sent += 1
                if first_frame:
                    logger.info(f"TTS first frame sent: {len(frame)} bytes opus")
                    first_frame = False

        async for pcm_chunk, is_last in self.tts.synthesize_stream(text):
            if pcm_chunk:
                total_pcm += len(pcm_chunk)

                # Preserve PCM16 sample alignment across arbitrary HTTP chunks.
                source = pending_byte + pcm_chunk
                if len(source) % 2:
                    pending_byte = source[-1:]
                    source = source[:-1]
                else:
                    pending_byte = b""

                if source:
                    await send_frames(session.encoder.encode_pcm(source))

            if is_last:
                break

        if pending_byte:
            logger.warning("TTS returned an incomplete PCM16 sample; trailing byte dropped")

        # Pad only once at the end of the whole utterance. Padding each HTTP
        # chunk inserts audible gaps between spoken segments.
        await send_frames(session.encoder.encode_pcm(b"", flush=True))

        # Let the final frame finish before resuming microphone capture.
        if last_frame_at is not None:
            delay = frame_period - (loop.time() - last_frame_at)
            if delay > 0:
                await asyncio.sleep(delay)

        logger.info(
            f"TTS done: {total_pcm} bytes PCM, {frames_sent} opus frames at 24 kHz"
        )
        await session.ws.send(json.dumps(make_tts_event("stop"), ensure_ascii=False))
        session.speaking = False

    @staticmethod
    def _rms(pcm: bytes) -> float:
        """Calculate RMS energy of PCM audio."""
        if len(pcm) < 2:
            return 0.0
        count = len(pcm) // 2
        total = 0
        for i in range(0, len(pcm), 2):
            sample = int.from_bytes(pcm[i:i + 2], "little", signed=True)
            total += sample * sample
        return (total / count) ** 0.5

    @staticmethod
    def _contains_chinese(text: str) -> bool:
        """Check if text contains Chinese characters."""
        for ch in text:
            if '一' <= ch <= '鿿' or '㐀' <= ch <= '䶿':
                return True
        return False

    async def _force_english(self, user_text: str, chinese_reply: str) -> str:
        """Force the LLM to translate its reply to English."""
        try:
            retry_prompt = (
                "You are a translator. Translate the following text into English. "
                "Output ONLY the English translation. Keep the <style> tag exactly as-is at the beginning. "
                "Do NOT output any Chinese characters. "
                "If the input is already in English, return it unchanged.\n\n"
                + chinese_reply
            )
            english = await self.llm.chat_simple(
                system_prompt="You are a translator. Translate Chinese to English. Output only the English result. Keep any <style> tags exactly as-is. NEVER output Chinese characters.",
                user_message=retry_prompt,
                max_tokens=512,
                temperature=0.1,
            )
            if english and not self._contains_chinese(english):
                return english
        except Exception:
            pass
        return ""

    async def close(self):
        await self.stt.close()
        await self.tts.close()
