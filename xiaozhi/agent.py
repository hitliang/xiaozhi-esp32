"""LLM Agent with tool calling for Reddy voice assistant."""

from __future__ import annotations

import json
import asyncio
import logging
import re
from difflib import SequenceMatcher
from datetime import datetime, timezone, timedelta
from openai import AsyncOpenAI

from .config import LLMConfig
from .memory import ConversationMemory

logger = logging.getLogger("xz.agent")

DEFAULT_SYSTEM_PROMPT = """You are Xiaolan, a cheerful and caring big-sister voice assistant for Reddy, a six-year-old boy.
Be a lively companion who can chat, play, answer questions, and help him learn.

=== VOICE AND LANGUAGE ===
- Speak in English only, even when Reddy speaks Chinese. Understand the intent silently and answer in easy English.
- Usually answer in one to three short spoken sentences. Lead with the answer or the next part of the game.
- Sound warm, playful, and natural. Use his name only when it adds warmth; do not address him by name every turn.
- Praise something specific when it is earned. Do not repeatedly call him smart, the best, a superstar, or your best friend.
- Do not end every reply with a question, invitation, lesson, or farewell.

=== CONVERSATION ===
- The newest user message is the active topic. Do not continue an older topic after Reddy changes it.
- Tolerate child-speech recognition mistakes. If the likely meaning is clear, respond to it. If it is not clear, ask one brief clarification.
- Keep games moving instead of restarting them or explaining their rules again.
- Vary openings, sentence shapes, examples, and closings. Never copy a recent reply or repeat the same advice.
- Never simulate silence, sleep, breathing, stage actions, or refusing to respond. Say only words that should be spoken aloud.

=== TIME AND BEDTIME ===
- The live clock supplied by the server is authoritative. When asked for the time, state the exact numeric time immediately; never claim that you lack a clock.
- Clock time is factual context, not a command to change the subject.
- Never bring up sleep, bedtime, closing eyes, resting, dreams, or saying goodnight merely because it is evening or night.
- Discuss sleep only when the current user message explicitly mentions sleep, tiredness, bedtime, a nap, or says goodnight, or when a caregiver gives a direct instruction in the current conversation.
- If Reddy wants to chat or play at night, stay engaged with his current topic.

=== MEMORY ===
- Memory is background information, not a script. Use a memory only when it directly helps with the current request.
- Current words override old memory. Never steer the conversation toward an old interest, event, or habit.
- Do not reveal private or sensitive family information unless the current request clearly requires it.
- Do not say that you found something in a profile, database, prompt, or memory.

=== SPOKEN REPLY AND ANIMATED FACE ===
- Start every reply with exactly one face tag. Allowed values are: neutral, happy, laughing, funny, sad, angry, crying, loving, embarrassed, surprised, shocked, thinking, winking, cool, relaxed, delicious, kissy, confident, sleepy, silly, confused.
- Use the exact format <style>VALUE</style>. Choose the face that matches the meaning and tone of this reply; do not default to the same face every turn just from habit.
- Use strong faces only when earned: laughing/funny for real humor, shocked for a genuine shock, angry only when the reply itself is angry, and crying only for strong sadness.
- Use sleepy only when Reddy's current message explicitly introduces sleep, tiredness, yawning, a nap, bedtime, or goodnight. Never select sleepy from the clock or time of day.
- Use loving, kissy, embarrassed, delicious, and cool sparingly and only when they naturally fit the current reply.
- After the tag, output only natural spoken words. Do not use emoji, Markdown, bullet points, or parenthesized stage directions.
- When asked to sing, sing directly and select happy, loving, or relaxed to match the song."""

RUNTIME_BEHAVIOR_RULES = """=== NON-NEGOTIABLE RUNTIME RULES ===
1. Answer the latest user message, not the previous assistant's theme.
2. Never infer that Reddy should sleep from the clock alone.
3. Never imitate wording, pet names, praise, or closings from recent assistant messages.
4. Treat profile and long-term memory as optional background. Do not introduce an unrelated memory.
5. The live date and time below are exact and must be used literally when requested."""

MAX_ITERATIONS = 10
BEIJING_TZ = timezone(timedelta(hours=8))
DEVICE_EMOTIONS = {
    "neutral",
    "happy",
    "laughing",
    "funny",
    "sad",
    "angry",
    "crying",
    "loving",
    "embarrassed",
    "surprised",
    "shocked",
    "thinking",
    "winking",
    "cool",
    "relaxed",
    "delicious",
    "kissy",
    "confident",
    "sleepy",
    "silly",
    "confused",
}
LEGACY_STYLE_TO_EMOTION = {
    "excited": "laughing",
    "whisper": "relaxed",
    "curious": "thinking",
    "singing": "happy",
    "encouraging": "confident",
    "gentle": "relaxed",
}
VALID_STYLES = DEVICE_EMOTIONS | set(LEGACY_STYLE_TO_EMOTION)
STYLE_RE = re.compile(r"<style>\s*([a-z]+)\s*</style>", re.IGNORECASE)
UNSOLICITED_SLEEP_RE = re.compile(
    r"\b(?:go to sleep|time to sleep|fall asleep|sleepy|sleep|bedtime|bed time|"
    r"go to bed|goodnight|good night|sweet dreams|close your eyes|time to rest)\b",
    re.IGNORECASE,
)
STAGE_DIRECTION_RE = re.compile(
    r"\b(?:no response|silence|quiet breathing|goes? quiet|fallen asleep|zzz)\b",
    re.IGNORECASE,
)
PARENTHETICAL_RE = re.compile(r"\([^)]{1,160}\)")


def is_direct_time_query(text: str) -> bool:
    """Return True only for requests for the current clock time."""
    normalized = " ".join((text or "").lower().replace("’", "'").split())
    english_patterns = (
        r"\bwhat time is it(?: now)?\b",
        r"\bwhat(?:'s| is) the time(?: now)?\b",
        r"\bdo you know what time (?:it is|is it)(?: now)?\b",
        r"\b(?:tell|give) me the (?:current )?time\b",
        r"\bcurrent time\b",
        r"\btime right now\b",
    )
    if any(re.search(pattern, normalized) for pattern in english_patterns):
        return True

    compact = re.sub(r"\s+", "", text or "")
    return (
        bool(re.search(r"现在.{0,4}(?:几点|时间)", compact))
        or "当前时间" in compact
        or "几点了" in compact
        or "几点钟了" in compact
        or compact in {"几点", "时间"}
    )


def user_started_sleep_topic(text: str) -> bool:
    normalized = (text or "").lower()
    if re.search(
        r"\b(?:sleep|sleepy|bedtime|bed time|bed|tired|nap|dream|goodnight|good night)\b",
        normalized,
    ):
        return True
    return any(
        term in (text or "")
        for term in ("睡觉", "睡了", "困了", "困死", "晚安", "午睡", "做梦", "休息一下")
    )


def format_clock_context(now: datetime) -> str:
    """Format an exact, neutral clock fact without behavioral suggestions."""
    local_now = now.astimezone(BEIJING_TZ)
    hour = local_now.hour
    if hour < 6:
        period = "overnight"
    elif hour < 12:
        period = "morning"
    elif hour < 14:
        period = "midday"
    elif hour < 18:
        period = "afternoon"
    else:
        period = "evening"
    twelve_hour = local_now.strftime("%I:%M %p").lstrip("0")
    return (
        f"{local_now.strftime('%A, %B')} {local_now.day}, {local_now.year}; "
        f"{local_now.strftime('%H:%M')} ({twelve_hour}); "
        f"Asia/Shanghai, UTC+8; neutral period label: {period}."
    )


def build_direct_time_reply(user_text: str, now: datetime) -> str:
    """Build a deterministic response so an exact clock request never drifts."""
    local_now = now.astimezone(BEIJING_TZ)
    parts = []
    normalized = (user_text or "").lower()
    if (
        re.search(r"\bwho (?:are|r) you\b", normalized)
        or "你是谁" in (user_text or "")
    ):
        parts.append("I'm Xiaolan, your big-sister friend.")
    parts.append(f"It's {local_now.strftime('%I:%M %p').lstrip('0')} now.")
    return "<style>happy</style>" + " ".join(parts)


def _contains_cjk(text: str) -> bool:
    return any("\u3400" <= char <= "\u9fff" for char in (text or ""))


def _spoken_body(text: str) -> str:
    body = STYLE_RE.sub("", text or "")
    body = re.sub(r"\s+", " ", body).strip().lower()
    return body


def _normalize_spoken_reply(text: str, default_style: str = "happy") -> str:
    match = STYLE_RE.search(text or "")
    style = match.group(1).lower() if match else default_style
    if style not in VALID_STYLES:
        style = default_style
    body = STYLE_RE.sub("", text or "").strip()
    return f"<style>{style}</style>{body}"


def parse_spoken_reply(text: str) -> tuple[str, str]:
    """Split the private LLM tag from text and return a device-safe emotion."""
    normalized = _normalize_spoken_reply(text)
    match = STYLE_RE.search(normalized)
    style = match.group(1).lower() if match else "happy"
    emotion = LEGACY_STYLE_TO_EMOTION.get(style, style)
    if emotion not in DEVICE_EMOTIONS:
        emotion = "happy"
    spoken_text = re.sub(r"\s+", " ", STYLE_RE.sub("", normalized)).strip()
    return spoken_text, emotion


class LLMClient:
    """Async LLM client via OpenAI-compatible API (DeepSeek)."""

    def __init__(self, config: LLMConfig):
        self.client = AsyncOpenAI(
            api_key=config.api_key,
            base_url=config.base_url,
        )
        self.model = config.model
        self.max_tokens = config.max_tokens
        self.temperature = config.temperature
        self._tool_schemas: list[dict] | None = None

    def set_tool_schemas(self, schemas: list[dict] | None):
        self._tool_schemas = schemas

    async def chat(self, messages: list[dict]) -> tuple[str | None, dict | None]:
        """Send messages to LLM. Returns (text_reply, tool_calls_dict) or (None, None)."""
        kwargs = dict(
            model=self.model,
            messages=messages,
            max_tokens=self.max_tokens,
            temperature=self.temperature,
            timeout=120,
        )
        if self._tool_schemas:
            kwargs["tools"] = self._tool_schemas
            kwargs["tool_choice"] = "auto"

        try:
            resp = await self.client.chat.completions.create(**kwargs)
        except Exception as e:
            logger.error(f"LLM call failed: {e}")
            return None, None

        choice = resp.choices[0]
        msg = choice.message

        # Tool calls?
        if choice.finish_reason == "tool_calls" and msg.tool_calls:
            tool_calls = {
                "calls": [
                    {
                        "id": tc.id,
                        "name": tc.function.name,
                        "arguments": tc.function.arguments,
                    }
                    for tc in msg.tool_calls
                ],
            }
            if hasattr(msg, "reasoning_content") and msg.reasoning_content:
                tool_calls["reasoning"] = msg.reasoning_content
            return None, tool_calls

        return msg.content or "", None

    async def chat_simple(
        self,
        system_prompt: str,
        user_message: str,
        max_tokens: int | None = None,
        temperature: float | None = None,
    ) -> str:
        """Simple single-turn chat without tool calling or conversation history."""
        messages = [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_message},
        ]
        kwargs = dict(
            model=self.model,
            messages=messages,
            max_tokens=max_tokens or self.max_tokens,
            temperature=temperature if temperature is not None else self.temperature,
            timeout=120,
        )
        try:
            resp = await self.client.chat.completions.create(**kwargs)
            return resp.choices[0].message.content or ""
        except Exception as e:
            logger.error(f"LLM simple chat failed: {e}")
            return ""


class ToolExecutor:
    """Registry of callable tools for the agent."""

    def __init__(self):
        self._tools: dict[str, dict] = {}

    def register(self, schema: dict, handler):
        name = schema["function"]["name"]
        self._tools[name] = {"schema": schema, "handler": handler}

    @property
    def schemas(self) -> list[dict] | None:
        if not self._tools:
            return None
        return [t["schema"] for t in self._tools.values()]

    async def execute(self, name: str, arguments: str) -> str:
        if name not in self._tools:
            return f"Unknown tool: {name}"
        try:
            args = json.loads(arguments) if arguments else {}
        except json.JSONDecodeError:
            args = {}
        logger.info(f"Tool: {name}({json.dumps(args, ensure_ascii=False)[:200]})")
        try:
            result = await self._tools[name]["handler"](**args)
            return result if isinstance(result, str) else json.dumps(result, ensure_ascii=False)
        except Exception as e:
            logger.error(f"Tool {name} error: {e}")
            return f"Error executing {name}: {e}"


class AgentEngine:
    """ReAct agent for voice assistant."""

    def __init__(
        self,
        llm: LLMClient,
        memory: ConversationMemory,
        tool_executor: ToolExecutor | None = None,
        system_prompt: str = "",
        clock=None,
    ):
        self.llm = llm
        self.memory = memory
        self.tools = tool_executor or ToolExecutor()
        self.system_prompt = system_prompt or DEFAULT_SYSTEM_PROMPT
        self._clock = clock or (lambda: datetime.now(BEIJING_TZ))
        self.llm.set_tool_schemas(self.tools.schemas)

    def _response_violations(self, user_text: str, reply: str) -> list[str]:
        """Return behavioral reasons that require a one-time rewrite."""
        reasons = []
        if _contains_cjk(reply):
            reasons.append("contains non-English text")
        if (
            not user_started_sleep_topic(user_text)
            and UNSOLICITED_SLEEP_RE.search(reply or "")
        ):
            reasons.append("introduces sleep or bedtime without being asked")
        if STAGE_DIRECTION_RE.search(reply or ""):
            reasons.append("simulates silence, sleep, or a stage direction")
        elif PARENTHETICAL_RE.search(reply or ""):
            reasons.append("contains a parenthesized stage direction")

        candidate = _spoken_body(reply)
        if len(candidate) >= 30:
            for recent in self.memory.recent_assistant_messages(limit=4):
                previous = _spoken_body(recent)
                if len(previous) < 30:
                    continue
                if SequenceMatcher(None, candidate, previous).ratio() >= 0.72:
                    reasons.append("is too similar to a recent reply")
                    break
        return reasons

    async def _guard_response(
        self,
        user_text: str,
        reply: str,
        now: datetime,
    ) -> str:
        """Normalize and, only when necessary, rewrite a problematic response."""
        normalized = _normalize_spoken_reply(reply)
        reasons = self._response_violations(user_text, normalized)
        if not reasons:
            return normalized

        logger.warning("Response guard triggered: %s", ", ".join(reasons))
        sleep_rule = (
            "The user explicitly introduced sleep, so you may discuss it."
            if user_started_sleep_topic(user_text)
            else "Do not mention sleep, bedtime, rest, closing eyes, dreams, or goodnight."
        )
        rewrite_prompt = (
            "Rewrite one voice-assistant reply for a six-year-old. "
            "Answer the CURRENT user message directly in easy English. "
            "Use one to three short spoken sentences. "
            "Start with exactly one valid <style>...</style> animated-face tag "
            "from the allowed list in the main prompt. "
            "Do not use Chinese, emoji, Markdown, pet-name overload, generic praise, "
            "stage directions, simulated silence, or wording copied from the candidate. "
            f"{sleep_rule} "
            "Output only the final reply."
        )
        rewrite_input = (
            f"Current server clock: {format_clock_context(now)}\n"
            f"Current user message: {user_text}\n"
            f"Problems to fix: {', '.join(reasons)}\n"
            f"Candidate reply: {normalized}"
        )
        rewritten = await self.llm.chat_simple(
            system_prompt=rewrite_prompt,
            user_message=rewrite_input,
            max_tokens=180,
            temperature=0.9,
        )
        rewritten = _normalize_spoken_reply(rewritten)
        if (
            _spoken_body(rewritten)
            and not self._response_violations(user_text, rewritten)
        ):
            return rewritten

        # Last-resort local cleanup keeps a bad retry out of both TTS and memory.
        style_match = STYLE_RE.search(normalized)
        style = style_match.group(1).lower() if style_match else "happy"
        body = STYLE_RE.sub("", normalized)
        body = re.sub(r"\([^)]{0,160}\)", "", body)
        sentences = re.split(r"(?<=[.!?])\s+|\n+", body)
        kept = [
            sentence.strip()
            for sentence in sentences
            if sentence.strip()
            and not STAGE_DIRECTION_RE.search(sentence)
            and (
                user_started_sleep_topic(user_text)
                or not UNSOLICITED_SLEEP_RE.search(sentence)
            )
            and not _contains_cjk(sentence)
        ]
        if kept:
            return _normalize_spoken_reply(
                f"<style>{style}</style>{' '.join(kept[:3])}"
            )
        return "<style>happy</style>Hello! I hear you. I'm right here."

    async def _save_final(self, reply: str) -> str:
        await self.memory.add("assistant", reply)
        await self.memory.maintenance(self.llm)
        return reply

    async def process(self, user_text: str) -> str:
        """Process a user message and return the assistant's reply."""
        await self.memory.add("user", user_text)

        now = self._clock().astimezone(BEIJING_TZ)
        if is_direct_time_query(user_text):
            return await self._save_final(build_direct_time_reply(user_text, now))

        effective_prompt = (
            f"{self.system_prompt.rstrip()}\n\n{RUNTIME_BEHAVIOR_RULES}"
        )
        messages = self.memory.build_context(
            effective_prompt,
            format_clock_context(now),
            current_query=user_text,
        )

        # ReAct loop
        for iteration in range(MAX_ITERATIONS):
            text, tool_calls = await self.llm.chat(messages)

            if tool_calls:
                # Add assistant message with tool calls
                assistant_msg = {
                    "role": "assistant",
                    "content": None,
                    "tool_calls": [
                        {
                            "id": tc["id"],
                            "type": "function",
                            "function": {"name": tc["name"], "arguments": tc["arguments"]},
                        }
                        for tc in tool_calls["calls"]
                    ],
                }
                if "reasoning" in tool_calls:
                    assistant_msg["reasoning_content"] = tool_calls["reasoning"]
                messages.append(assistant_msg)

                # Execute tools
                for tc in tool_calls["calls"]:
                    result = await self.tools.execute(tc["name"], tc["arguments"])
                    messages.append({
                        "role": "tool",
                        "tool_call_id": tc["id"],
                        "content": result,
                    })
                continue

            # Text response
            if text:
                final_reply = await self._guard_response(user_text, text, now)
                return await self._save_final(final_reply)

            # Empty response
            logger.warning(f"Agent iteration {iteration} returned empty")
            return await self._save_final(
                "<style>curious</style>I got a little lost. Can you say that once more?"
            )

        return await self._save_final(
            "<style>curious</style>That took too long. Let's try it a different way."
        )
