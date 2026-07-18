"""Focused conversation memory for the Reddy voice assistant.

The SQLite message table is an archive, not an LLM replay buffer. A new device
connection starts a clean working session, the model sees only a short recent
tail, and older active-session turns are summarized when needed. Long-term facts
and profile fields are retrieved only when they match the current user request.
"""

from __future__ import annotations

import json
import logging
import re
from difflib import SequenceMatcher
from datetime import datetime
from typing import List, Dict, Optional, Tuple

logger = logging.getLogger("xz.memory")

# ── Working-memory limits ───────────────────────────────────────────
# The database remains a conversation archive. Only the active session is used
# as dialogue context, and only a small tail is sent to the model each turn.
MAX_CONTEXT_MESSAGES = 20
MAX_CONTEXT_TOKENS = 12_000
SUMMARY_TRIGGER_MESSAGES = 40
SUMMARY_TRIGGER_TOKENS = 6_000
KEEP_RECENT_AFTER_SUMMARY = 16
FACT_EXTRACTION_INTERVAL = 10
PROFILE_UPDATE_INTERVAL = 30
MAX_RELEVANT_FACTS = 6

SEARCH_STOPWORDS = {
    "a", "an", "and", "are", "about", "can", "do", "does", "for", "how",
    "i", "is", "it", "me", "my", "of", "please", "reddy", "tell", "the",
    "this", "to", "user", "what", "when", "where", "who", "why", "you", "your",
}

# A tiny bilingual bridge is enough for deterministic retrieval of Reddy's
# English memories when speech recognition returns Chinese.
CHINESE_SEARCH_TERMS = {
    "生日": {"birthday"},
    "几岁": {"age"},
    "名字": {"name"},
    "钢琴": {"piano"},
    "英语": {"english"},
    "汽车": {"car", "vehicle"},
    "小汽车": {"car", "vehicle"},
    "火车": {"train", "vehicle"},
    "公交": {"bus", "vehicle"},
    "飞机": {"airplane", "trip"},
    "机场": {"airport", "airplane", "trip"},
    "同学": {"classmate", "student"},
    "学校": {"school", "grade"},
    "年级": {"grade", "school"},
    "篮球": {"basketball"},
    "游泳": {"swimming"},
    "唱歌": {"singing", "song"},
    "睡": {"sleep"},
    "爷爷": {"grandpa"},
    "奶奶": {"grandma"},
    "爸爸": {"dad", "father"},
    "妈妈": {"mom", "mother"},
}

# User profile template
DEFAULT_PROFILE = {
    "name": "Reddy",
    "chinese_name": "瑞迪",
    "age": 6,
    "grade": "1st grade",
    "interests": [],
    "dislikes": [],
    "important_dates": [],
    "preferences": {},
    "notes": "",
    "last_updated": "",
}


def estimate_tokens(text: str) -> int:
    """Estimate token count for mixed Chinese/English text.
    Uses cl100k_base (GPT-4 / DeepSeek-like) tokenizer if available,
    falls back to ~0.7 chars/token for Chinese, ~0.25 for English.
    """
    try:
        import tiktoken
        enc = tiktoken.get_encoding("cl100k_base")
        return len(enc.encode(text))
    except (ImportError, Exception):
        # Fallback: rough heuristic for mixed Chinese text
        chars = len(text)
        # Chinese chars ~1.5 tokens/char, ASCII ~0.25 tokens/char
        ascii_count = sum(1 for c in text if ord(c) < 128)
        cjk_count = chars - ascii_count
        return int(ascii_count * 0.3 + cjk_count * 0.7)


class ConversationMemory:
    """Four-tier memory manager for a single user (Reddy)."""

    def __init__(self, db_path: str = "./data/reddy_memory.db"):
        self._db_path = db_path
        self._messages: List[Tuple[str, str, int]] = []  # (role, content, tokens)
        self._summary: str = ""
        self._summary_tokens: int = 0
        self._facts: List[Dict] = []          # [{"content":..., "category":..., "importance":...}]
        self._profile: Dict = dict(DEFAULT_PROFILE)
        self._classmates: List[Tuple[int, str]] = []  # [(student_number, name), ...]
        self._total_recent_tokens: int = 0
        self._turn_count: int = 0

    # ── Database ─────────────────────────────────────────────────
    async def _get_db(self):
        import aiosqlite
        db = await aiosqlite.connect(self._db_path)
        await db.execute("PRAGMA journal_mode=WAL")
        await db.execute("PRAGMA synchronous=NORMAL")
        return db

    async def load_or_create(self):
        db = await self._get_db()
        try:
            await db.execute(
                "CREATE TABLE IF NOT EXISTS messages ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  role TEXT NOT NULL, content TEXT NOT NULL,"
                "  token_count INTEGER DEFAULT 0,"
                "  created_at TEXT DEFAULT (datetime('now', '+8 hours'))"
                ")"
            )
            await db.execute(
                "CREATE TABLE IF NOT EXISTS session_summary ("
                "  id INTEGER PRIMARY KEY, content TEXT NOT NULL,"
                "  token_count INTEGER DEFAULT 0,"
                "  updated_at TEXT DEFAULT (datetime('now', '+8 hours'))"
                ")"
            )
            await db.execute(
                "CREATE TABLE IF NOT EXISTS facts ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  content TEXT NOT NULL,"
                "  category TEXT DEFAULT 'general',"
                "  importance INTEGER DEFAULT 5,"
                "  created_at TEXT DEFAULT (datetime('now', '+8 hours')),"
                "  updated_at TEXT DEFAULT (datetime('now', '+8 hours'))"
                ")"
            )
            await db.execute(
                "CREATE TABLE IF NOT EXISTS user_profile ("
                "  id INTEGER PRIMARY KEY,"
                "  profile_json TEXT NOT NULL,"
                "  updated_at TEXT DEFAULT (datetime('now', '+8 hours'))"
                ")"
            )
            await db.execute(
                "CREATE TABLE IF NOT EXISTS classmates ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  student_number INTEGER NOT NULL,"
                "  name TEXT NOT NULL,"
                "  created_at TEXT DEFAULT (datetime('now', '+8 hours'))"
                ")"
            )
            await db.commit()

            # A saved summary is kept for the admin archive, but it must not
            # become the starting topic of a brand-new voice session.
            cursor = await db.execute("SELECT content, token_count FROM session_summary WHERE id=1")
            row = await cursor.fetchone()
            archived_summary_tokens = row[1] if row else 0
            self._summary = ""
            self._summary_tokens = 0

            # Load facts
            cursor = await db.execute(
                "SELECT content, category, importance FROM facts ORDER BY importance DESC, id DESC"
            )
            rows = await cursor.fetchall()
            self._facts = [
                {"content": r[0], "category": r[1], "importance": r[2]}
                for r in rows
            ]

            # Load profile
            cursor = await db.execute("SELECT profile_json FROM user_profile WHERE id=1")
            row = await cursor.fetchone()
            if row:
                saved = json.loads(row[0])
                self._profile = self._sanitize_profile(
                    {**self._profile, **saved}
                )

            # Load classmates
            cursor = await db.execute(
                "SELECT student_number, name FROM classmates ORDER BY student_number"
            )
            rows = await cursor.fetchall()
            self._classmates = [(r[0], r[1]) for r in rows]

            # Messages in SQLite are an archive for the admin UI and memory
            # maintenance. They are deliberately not replayed into a new
            # session; replaying hundreds of old assistant replies causes topic
            # fixation and wording loops.
            cursor = await db.execute(
                "SELECT COUNT(*), COALESCE(SUM(token_count), 0) FROM messages"
            )
            archived_messages, archived_tokens = await cursor.fetchone()
            self.start_session()

        finally:
            await db.close()

        logger.info(
            f"Memory archive: {archived_messages} msgs ({archived_tokens}t), "
            f"archived summary {archived_summary_tokens}t; active session starts empty; "
            f"{len(self._facts)} facts, {len(self._classmates)} classmates, "
            f"profile_age={self._profile.get('age','?')}"
        )

    # ── Active session ───────────────────────────────────────────
    def start_session(self):
        """Start a clean working-memory session without deleting the archive."""
        self._messages = []
        self._summary = ""
        self._summary_tokens = 0
        self._total_recent_tokens = 0
        self._turn_count = 0
        logger.info("Started a clean conversation session")

    def recent_assistant_messages(self, limit: int = 4) -> List[str]:
        replies = [
            content
            for role, content, _ in reversed(self._messages)
            if role == "assistant"
        ]
        return replies[:limit]

    # ── Add messages ─────────────────────────────────────────────
    async def add(self, role: str, content: str):
        tokens = estimate_tokens(content)
        self._messages.append((role, content, tokens))
        self._total_recent_tokens += tokens
        if role == "user":
            self._turn_count += 1
        # Persist immediately so admin page sees it
        await self._insert_message(role, content, tokens)

    # ── Retrieval helpers ─────────────────────────────────────────
    @staticmethod
    def _search_terms(text: str) -> set[str]:
        lowered = (text or "").lower()
        terms = set()
        for token in re.findall(r"[a-z0-9]+", lowered):
            if token in SEARCH_STOPWORDS or len(token) < 2:
                continue
            terms.add(token)
            if len(token) > 4 and token.endswith("s"):
                terms.add(token[:-1])
        if terms & {"like", "likes", "favorite", "favourite"}:
            terms.add("interest")
        if terms & {"dislike", "dislikes", "hate"}:
            terms.add("preference")
        for chinese, mapped in CHINESE_SEARCH_TERMS.items():
            if chinese in (text or ""):
                terms.update(mapped)
        return terms

    @classmethod
    def _relevance_score(cls, query: str, candidate: str) -> int:
        query_terms = cls._search_terms(query)
        if not query_terms:
            return 0
        candidate_terms = cls._search_terms(candidate)
        return len(query_terms & candidate_terms)

    # ── Build LLM context ────────────────────────────────────────
    def build_context(
        self,
        system_prompt: str,
        current_time: str,
        current_query: str = "",
    ) -> list[dict]:
        """Build focused context: live clock, relevant memory, short dialogue tail."""
        system_text = (
            f"{system_prompt.rstrip()}\n\n"
            "=== AUTHORITATIVE LIVE CLOCK ===\n"
            f"{current_time}\n"
            "Use the exact numeric value when asked. This is a factual clock reading only; "
            "it is never a reason to suggest sleep, bedtime, or ending the conversation."
        )

        memory_parts = []
        profile_text = self._format_profile(current_query)
        if profile_text:
            memory_parts.append(f"Relevant profile:\n{profile_text}")

        facts_text = self._format_facts(current_query)
        if facts_text:
            memory_parts.append(f"Relevant long-term facts:\n{facts_text}")

        classmates_text = self._format_classmates(current_query)
        if classmates_text:
            memory_parts.append(f"Requested classmate data:\n{classmates_text}")

        if memory_parts:
            system_text += (
                "\n\n=== RELEVANT MEMORY ===\n"
                "Background only. It may be stale. Use it only if it directly answers "
                "the current user message, and never introduce its topics on your own.\n"
                + "\n".join(memory_parts)
            )

        if self._summary:
            system_text += (
                "\n\n=== ACTIVE-SESSION SUMMARY ===\n"
                "Use only for continuity. Do not imitate earlier assistant phrasing or advice.\n"
                f"{self._summary}"
            )

        budget = max(1_000, MAX_CONTEXT_TOKENS - estimate_tokens(system_text))
        tail = self._messages[-MAX_CONTEXT_MESSAGES:]
        included = []
        included_tokens = 0
        for role, content, tokens in reversed(tail):
            if included and included_tokens + tokens > budget:
                break
            included.insert(0, {"role": role, "content": content})
            included_tokens += tokens

        # Never start the conversational tail with an orphaned assistant reply.
        while included and included[0]["role"] != "user":
            included.pop(0)

        omitted = len(self._messages) - len(included)
        if omitted > 0:
            logger.debug(
                "Context kept %s active-session messages (%st), omitted %s",
                len(included),
                included_tokens,
                omitted,
            )

        return [{"role": "system", "content": system_text}, *included]

    # ── Formatting helpers ───────────────────────────────────────
    @staticmethod
    def _sanitize_profile(profile: Dict) -> Dict:
        """Keep the profile compact, predictable, and free of schema drift."""
        clean = dict(DEFAULT_PROFILE)
        for key in ("name", "chinese_name", "age", "grade", "last_updated"):
            if key in profile:
                clean[key] = profile[key]

        for key in ("interests", "dislikes"):
            values = profile.get(key, [])
            if isinstance(values, list):
                unique = []
                seen = set()
                for value in values:
                    if not isinstance(value, str):
                        continue
                    value = value.strip()[:160]
                    normalized = value.lower()
                    if value and normalized not in seen:
                        seen.add(normalized)
                        unique.append(value)
                clean[key] = unique[:24]

        dates = profile.get("important_dates", [])
        if isinstance(dates, list):
            clean["important_dates"] = [
                {
                    "event": str(item.get("event", "")).strip()[:100],
                    "date": str(item.get("date", "")).strip()[:40],
                }
                for item in dates
                if isinstance(item, dict) and item.get("event")
            ][:20]

        preferences = profile.get("preferences", {})
        if isinstance(preferences, dict):
            clean["preferences"] = {
                str(key).strip()[:60]: str(value).strip()[:180]
                for key, value in list(preferences.items())[:20]
                if str(key).strip() and str(value).strip()
            }

        notes = profile.get("notes", "")
        if isinstance(notes, str):
            clean["notes"] = notes.strip()[:1_000]
        return clean

    def _format_profile(self, query: str) -> str:
        p = self._profile
        parts = [
            f"- Identity: {p.get('name', 'Reddy')}, age {p.get('age', 6)}, "
            f"{p.get('grade', '1st grade')}."
        ]

        candidates = []
        for item in p.get("interests", []):
            candidates.append(f"Interest: {item}")
        for item in p.get("dislikes", []):
            candidates.append(f"Preference/dislike: {item}")
        for key, value in p.get("preferences", {}).items():
            candidates.append(f"Preference {key}: {value}")
        for item in p.get("important_dates", []):
            if isinstance(item, dict):
                candidates.append(
                    f"Important date: {item.get('event', '')} is {item.get('date', '')}"
                )

        scored = [
            (self._relevance_score(query, item), index, item)
            for index, item in enumerate(candidates)
        ]
        scored = [item for item in scored if item[0] > 0]
        scored.sort(key=lambda item: (-item[0], item[1]))
        parts.extend(f"- {item[2]}" for item in scored[:3])
        return "\n".join(parts)

    def _format_facts(self, query: str) -> str:
        scored = []
        for index, fact in enumerate(self._facts):
            content = fact.get("content", "")
            score = self._relevance_score(query, content)
            if score:
                scored.append(
                    (score, int(fact.get("importance", 5)), -index, content)
                )
        scored.sort(reverse=True)
        return "\n".join(
            f"- {item[3]}" for item in scored[:MAX_RELEVANT_FACTS]
        )

    def _format_classmates(self, query: str) -> str:
        if not self._classmates:
            return ""

        query_text = query or ""
        lowered = query_text.lower()
        asks_classmates = (
            any(word in lowered for word in ("classmate", "student number", "student #"))
            or "同学" in query_text
            or "学号" in query_text
        )
        named_matches = [
            (sn, name) for sn, name in self._classmates if name in query_text
        ]
        number_match = re.search(r"(?:#|number\s*|学号\s*|第)?(\d{1,2})(?:号)?", lowered)

        selected = []
        if number_match and asks_classmates:
            number = int(number_match.group(1))
            selected = [(sn, name) for sn, name in self._classmates if sn == number]
        elif named_matches:
            selected = named_matches
        elif asks_classmates:
            selected = self._classmates
        else:
            return ""

        return "\n".join(
            f"#{sn} {name}{' (Reddy)' if sn == 39 else ''}"
            for sn, name in selected
        )

    # ── Auto-summarization ───────────────────────────────────────
    async def maybe_summarize(self, llm):
        """Condense a long active session while keeping its newest turns verbatim."""
        if (
            self._total_recent_tokens < SUMMARY_TRIGGER_TOKENS
            and len(self._messages) < SUMMARY_TRIGGER_MESSAGES
        ):
            return

        logger.info(f"Summarizing: {len(self._messages)} msgs, {self._total_recent_tokens}t")

        # Keep last KEEP_RECENT_AFTER_SUMMARY messages as-is
        split = max(10, len(self._messages) - KEEP_RECENT_AFTER_SUMMARY)
        old = self._messages[:split]
        recent = self._messages[split:]

        # Build conversation text for continuity. Assistant wording is included
        # only so events make sense; the summary prompt explicitly rejects
        # assistant style and unsolicited advice.
        conv_text = ""
        for role, content, _ in old:
            conv_text += f"[{role}]: {content}\n"

        existing = f"Previous summary: {self._summary}\n\n" if self._summary else ""
        prompt = (
            "Create a compact continuity summary of this ACTIVE voice session in "
            "120-250 English words. Preserve only user goals, game state, unanswered "
            "questions, and facts the user explicitly said. Do not preserve assistant "
            "wording, pet names, praise, stage directions, repeated phrases, or advice. "
            "Do not mention sleep or bedtime unless the user explicitly made it the "
            "topic. Output only the summary."
        )

        try:
            resp = await llm.chat_simple(
                system_prompt=prompt,
                user_message=f"{existing}对话记录:\n{conv_text[-8000:]}",  # limit for summarization
                max_tokens=1200,
                temperature=0.3,
            )
            if resp:
                self._summary = resp
                self._summary_tokens = estimate_tokens(resp)
                self._messages = recent
                self._total_recent_tokens = sum(t for _, _, t in recent)
                await self._persist_summary()
                logger.info(
                    f"Summary: {len(resp)} chars, kept {len(recent)} msgs ({self._total_recent_tokens}t)"
                )
        except Exception as e:
            logger.error(f"Summarization failed: {e}")

    # ── Fact extraction ──────────────────────────────────────────
    async def maybe_extract_facts(self, llm):
        """Extract long-term facts from recent conversation (Tier 3)."""
        if self._turn_count % FACT_EXTRACTION_INTERVAL != 0 or self._turn_count == 0:
            return
        if len(self._messages) < 5:
            return

        # Facts must come from the user's words, never from the assistant's
        # suggestions or repeated conversational habits.
        recent = [
            message for message in self._messages if message[0] == "user"
        ][-20:]
        conv = ""
        for role, content, _ in recent:
            conv += f"[{role}]: {content}\n"

        prompt = (
            "Extract durable information explicitly stated by the user, a six-year-old "
            "boy named Reddy. Never infer a fact from the assistant's words. Ignore "
            "one-off play actions, greetings, current mood, temporary plans without a "
            "date, bedtime resistance, repeated wording, and model-generated praise. "
            "Extract only objective facts that would still help in a future session. "
            "Output a JSON array, each item containing:\n"
            "- content: fact description (concise, one sentence, in English)\n"
            "- category: one of (interest/personality/preference/event/family/other)\n"
            "- importance: 1-10\n"
            "If there is no notable new information, return an empty array []. "
            "ALL content must be in English."
        )

        try:
            resp = await llm.chat_simple(
                system_prompt=prompt,
                user_message=conv,
                max_tokens=1000,
                temperature=0.2,
            )
            if not resp:
                return
            # Parse JSON array from response
            data = self._parse_json_from_response(resp)
            if not data or not isinstance(data, list):
                return

            # Merge with existing facts (avoid duplicates, update conflicts)
            new_count = 0
            for item in data:
                content = item.get("content", "").strip()
                if not content or len(content) < 3:
                    continue
                # Check for a semantically near-duplicate existing fact.
                if any(self._similar(content, f["content"]) for f in self._facts):
                    continue
                self._facts.append({
                    "content": content,
                    "category": item.get("category", "general"),
                    "importance": int(item.get("importance", 5)),
                })
                new_count += 1

            if new_count:
                # Sort by importance desc, trim to 50
                self._facts.sort(key=lambda f: f["importance"], reverse=True)
                if len(self._facts) > 50:
                    self._facts = self._facts[:50]
                await self._persist_facts()
                logger.info(f"Extracted {new_count} new facts (total {len(self._facts)})")

        except Exception as e:
            logger.error(f"Fact extraction failed: {e}")

    # ── Profile update ───────────────────────────────────────────
    async def maybe_update_profile(self, llm):
        """Periodically update the user profile from facts and recent conversation (Tier 4)."""
        if (
            self._turn_count % PROFILE_UPDATE_INTERVAL != 0
            or self._turn_count == 0
        ):
            return

        current_profile = json.dumps(self._profile, ensure_ascii=False)
        conv = ""
        user_messages = [
            message for message in self._messages if message[0] == "user"
        ][-20:]
        for role, content, _ in user_messages:
            conv += f"[{role}]: {content}\n"

        prompt = (
            "Update a child's long-term profile using only facts explicitly stated in "
            "the USER messages. Ignore assistant text, role-play details, greetings, "
            "temporary game actions, repeated wording, bedtime behavior, and guesses. "
            "Keep only these top-level fields: name, chinese_name, age, grade, interests, "
            "dislikes, important_dates, preferences, notes, last_updated. "
            "Do not create nested user, family, or assistant objects. Merge duplicates. "
            "important_dates must be [{\"event\":\"\",\"date\":\"\"}].\n\n"
            f"Current profile: {current_profile}\n\n"
            "Output the complete updated profile JSON with no explanatory text. "
            "Except for chinese_name, all field values must be in English."
        )

        try:
            resp = await llm.chat_simple(
                system_prompt=prompt,
                user_message=f"最新对话:\n{conv}",
                max_tokens=800,
                temperature=0.2,
            )
            if not resp:
                return
            new_profile = self._parse_json_from_response(resp)
            if not new_profile or not isinstance(new_profile, dict):
                return
            new_profile["last_updated"] = datetime.now().strftime("%Y-%m-%d")
            self._profile = self._sanitize_profile(new_profile)
            await self._persist_profile()
            logger.info("Profile updated")
        except Exception as e:
            logger.error(f"Profile update failed: {e}")

    # ── Full memory maintenance cycle ────────────────────────────
    async def maintenance(self, llm):
        """Run all memory maintenance tasks."""
        await self.maybe_summarize(llm)
        await self.maybe_extract_facts(llm)
        await self.maybe_update_profile(llm)

    # ── Utility ──────────────────────────────────────────────────
    @staticmethod
    def _similar(a: str, b: str, threshold: float = 0.82) -> bool:
        """Check near-duplicate facts without conflating unrelated sentences."""
        normalize = lambda value: re.sub(r"[^a-z0-9]+", " ", value.lower()).strip()
        left, right = normalize(a), normalize(b)
        if left == right:
            return True
        if not left or not right:
            return False
        if SequenceMatcher(None, left, right).ratio() >= threshold:
            return True
        left_words, right_words = set(left.split()), set(right.split())
        if not left_words or not right_words:
            return False
        overlap = len(left_words & right_words) / len(left_words | right_words)
        return overlap >= 0.7

    @staticmethod
    def _parse_json_from_response(text: str):
        """Extract JSON from LLM response (may be wrapped in markdown or have extra text)."""
        text = text.strip()
        # Try direct parse
        try:
            return json.loads(text)
        except (json.JSONDecodeError, ValueError):
            pass
        # Try extracting from ```json ... ``` block
        if "```" in text:
            try:
                start = text.index("```") + 3
                if text[start:start + 4].lower() == "json":
                    start += 4
                end = text.index("```", start)
                return json.loads(text[start:end].strip())
            except (ValueError, json.JSONDecodeError):
                pass
        # Try finding first [ or {
        for bracket in ["[", "{"]:
            try:
                start = text.index(bracket)
                end = text.rindex("]" if bracket == "[" else "}")
                return json.loads(text[start:end + 1])
            except (ValueError, json.JSONDecodeError):
                pass
        return None

    # ── Persistence ──────────────────────────────────────────────
    async def _insert_message(self, role: str, content: str, tokens: int):
        db = await self._get_db()
        try:
            await db.execute(
                "INSERT INTO messages (role, content, token_count) VALUES (?, ?, ?)",
                (role, content, tokens),
            )
            await db.commit()
        finally:
            await db.close()

    async def _persist_summary(self):
        db = await self._get_db()
        try:
            await db.execute("DELETE FROM session_summary")
            await db.execute(
                "INSERT INTO session_summary (id, content, token_count) VALUES (1, ?, ?)",
                (self._summary, self._summary_tokens),
            )
            await db.commit()
        finally:
            await db.close()

    async def _persist_messages(self):
        db = await self._get_db()
        try:
            await db.execute("DELETE FROM messages")
            for role, content, tokens in self._messages:
                await db.execute(
                    "INSERT INTO messages (role, content, token_count) VALUES (?, ?, ?)",
                    (role, content, tokens),
                )
            await db.commit()
        finally:
            await db.close()

    async def _persist_facts(self):
        db = await self._get_db()
        try:
            await db.execute("DELETE FROM facts")
            for f in self._facts:
                await db.execute(
                    "INSERT INTO facts (content, category, importance) VALUES (?, ?, ?)",
                    (f["content"], f.get("category", "general"), f.get("importance", 5)),
                )
            await db.commit()
        finally:
            await db.close()

    async def _persist_profile(self):
        db = await self._get_db()
        try:
            profile_json = json.dumps(self._profile, ensure_ascii=False)
            await db.execute("DELETE FROM user_profile")
            await db.execute(
                "INSERT INTO user_profile (id, profile_json) VALUES (1, ?)",
                (profile_json,),
            )
            await db.commit()
        finally:
            await db.close()

    async def clear(self):
        self._messages = []
        self._summary = ""
        self._summary_tokens = 0
        self._facts = []
        self._profile = dict(DEFAULT_PROFILE)
        self._classmates = []
        self._total_recent_tokens = 0
        self._turn_count = 0
        db = await self._get_db()
        try:
            for table in ["messages", "session_summary", "facts", "user_profile", "classmates"]:
                await db.execute(f"DELETE FROM {table}")
            await db.commit()
        finally:
            await db.close()
