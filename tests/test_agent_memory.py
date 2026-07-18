import sys
import types
import unittest
from datetime import datetime


# The production server has openai installed. Unit tests exercise pure agent
# policy and use a fake client, so a tiny import stub keeps local tests isolated.
if "openai" not in sys.modules:
    openai_stub = types.ModuleType("openai")
    openai_stub.AsyncOpenAI = object
    sys.modules["openai"] = openai_stub

from xiaozhi.agent import (
    AgentEngine,
    BEIJING_TZ,
    build_direct_time_reply,
    format_clock_context,
    is_direct_time_query,
    parse_spoken_reply,
)
from xiaozhi.protocol import make_llm_emotion
from xiaozhi.memory import ConversationMemory, MAX_CONTEXT_MESSAGES


class FakeLLM:
    def __init__(self, reply="", rewrite=""):
        self.reply = reply
        self.rewrite = rewrite
        self.chat_calls = 0
        self.simple_calls = 0
        self.last_messages = None

    def set_tool_schemas(self, schemas):
        self.schemas = schemas

    async def chat(self, messages):
        self.chat_calls += 1
        self.last_messages = messages
        return self.reply, None

    async def chat_simple(self, **kwargs):
        self.simple_calls += 1
        return self.rewrite


class FakeMemory:
    def __init__(self):
        self.messages = []
        self.maintenance_calls = 0

    async def add(self, role, content):
        self.messages.append((role, content))

    def build_context(self, system_prompt, current_time, current_query=""):
        return [
            {
                "role": "system",
                "content": f"{system_prompt}\nCLOCK={current_time}",
            },
            {"role": "user", "content": current_query},
        ]

    def recent_assistant_messages(self, limit=4):
        return [
            content
            for role, content in reversed(self.messages)
            if role == "assistant"
        ][:limit]

    async def maintenance(self, llm):
        self.maintenance_calls += 1


class TimePolicyTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.fixed_now = datetime(2026, 7, 17, 22, 13, tzinfo=BEIJING_TZ)

    def test_current_time_intent_is_precise_but_event_time_is_not(self):
        self.assertTrue(is_direct_time_query("What time is it now?"))
        self.assertTrue(is_direct_time_query("Do you know what time is it now?"))
        self.assertTrue(is_direct_time_query("现在几点？"))
        self.assertFalse(is_direct_time_query("What time is the airplane tomorrow?"))
        self.assertFalse(is_direct_time_query("明天飞机几点？"))

    def test_direct_time_reply_uses_exact_clock_without_bedtime(self):
        reply = build_direct_time_reply("Who are you? What time is it now?", self.fixed_now)
        self.assertEqual(
            reply,
            "<style>happy</style>I'm Xiaolan, your big-sister friend. It's 10:13 PM now.",
        )
        self.assertNotIn("sleep", reply.lower())
        self.assertNotIn("bed", reply.lower())

    def test_clock_context_is_exact_and_neutral(self):
        context = format_clock_context(self.fixed_now)
        self.assertIn("22:13 (10:13 PM)", context)
        self.assertIn("Asia/Shanghai, UTC+8", context)
        self.assertNotIn("should", context.lower())
        self.assertNotIn("bedtime", context.lower())

    async def test_time_query_bypasses_llm_and_is_saved(self):
        llm = FakeLLM(reply="<style>gentle</style>I do not have a clock.")
        memory = FakeMemory()
        agent = AgentEngine(llm, memory, clock=lambda: self.fixed_now)

        reply = await agent.process("What time is it now?")

        self.assertEqual(llm.chat_calls, 0)
        self.assertIn("10:13 PM", reply)
        self.assertEqual(memory.messages[-1], ("assistant", reply))

    async def test_unsolicited_sleep_reply_is_rewritten_before_memory(self):
        llm = FakeLLM(
            reply="<style>gentle</style>Close your eyes and go to sleep. Goodnight!",
            rewrite="<style>happy</style>Hello! I hear you loud and clear.",
        )
        memory = FakeMemory()
        agent = AgentEngine(llm, memory, clock=lambda: self.fixed_now)

        reply = await agent.process("Hello hello, how are you?")

        self.assertEqual(llm.simple_calls, 1)
        self.assertEqual(reply, "<style>happy</style>Hello! I hear you loud and clear.")
        self.assertNotIn("sleep", memory.messages[-1][1].lower())
        self.assertNotIn("goodnight", memory.messages[-1][1].lower())

    async def test_goodnight_is_allowed_when_user_started_the_topic(self):
        llm = FakeLLM(
            reply="<style>gentle</style>Goodnight! See you next time.",
        )
        memory = FakeMemory()
        agent = AgentEngine(llm, memory, clock=lambda: self.fixed_now)

        reply = await agent.process("Goodnight.")

        self.assertEqual(llm.simple_calls, 0)
        self.assertIn("Goodnight", reply)

    async def test_repeated_reply_and_stage_direction_are_rewritten(self):
        repeated = "<style>happy</style>Hello! I am right here and ready to play."
        llm = FakeLLM(
            reply=repeated + " (giggles)",
            rewrite="<style>curious</style>What shall we build today?",
        )
        memory = FakeMemory()
        memory.messages.append(("assistant", repeated))
        agent = AgentEngine(llm, memory, clock=lambda: self.fixed_now)

        reply = await agent.process("Let us play.")

        self.assertEqual(llm.simple_calls, 1)
        self.assertEqual(reply, "<style>curious</style>What shall we build today?")


class EmotionProtocolTests(unittest.TestCase):
    def test_rich_face_tag_is_removed_from_spoken_text(self):
        text, emotion = parse_spoken_reply(
            "<style>surprised</style>Whoa, that tower is huge!"
        )
        self.assertEqual(text, "Whoa, that tower is huge!")
        self.assertEqual(emotion, "surprised")
        self.assertNotIn("<style>", text)

    def test_legacy_voice_styles_map_to_supported_device_faces(self):
        cases = {
            "excited": "laughing",
            "curious": "thinking",
            "encouraging": "confident",
            "gentle": "relaxed",
            "singing": "happy",
            "whisper": "relaxed",
        }
        for style, expected in cases.items():
            with self.subTest(style=style):
                text, emotion = parse_spoken_reply(
                    f"<style>{style}</style>Hello there."
                )
                self.assertEqual(text, "Hello there.")
                self.assertEqual(emotion, expected)

    def test_invalid_face_falls_back_without_leaking_control_tag(self):
        text, emotion = parse_spoken_reply(
            "<style>notarealface</style>Still safe to say."
        )
        self.assertEqual(text, "Still safe to say.")
        self.assertEqual(emotion, "happy")

    def test_llm_emotion_message_matches_device_protocol(self):
        self.assertEqual(
            make_llm_emotion("loving"),
            {"type": "llm", "emotion": "loving"},
        )


class MemoryContextTests(unittest.TestCase):
    def make_memory(self):
        memory = ConversationMemory(":memory:")
        memory._profile = {
            "name": "Reddy",
            "chinese_name": "瑞迪",
            "age": 6,
            "grade": "1st grade",
            "interests": ["Cars and track-based vehicles", "Piano"],
            "dislikes": ["Being rushed to sleep"],
            "important_dates": [{"event": "Birthday", "date": "2019-07-07"}],
            "preferences": {},
            "notes": "Resists bedtime and asks many questions.",
            "last_updated": "2026-07-17",
        }
        memory._facts = [
            {
                "content": "Reddy does not like being rushed to sleep",
                "category": "preference",
                "importance": 7,
            },
            {
                "content": "Reddy loves cars, trains, and track-based vehicles",
                "category": "interest",
                "importance": 9,
            },
        ]
        memory._classmates = [(1, "Student One"), (39, "Reddy")]
        memory._messages = [
            (
                "user" if index % 2 == 0 else "assistant",
                f"old message {index}",
                4,
            )
            for index in range(60)
        ]
        memory._messages.append(("user", "Tell me about cars", 5))
        return memory

    def test_context_retrieves_only_relevant_memory_and_short_tail(self):
        memory = self.make_memory()
        context = memory.build_context(
            "BASE PROMPT",
            "Friday, July 17, 2026; 22:13 (10:13 PM); Asia/Shanghai, UTC+8.",
            current_query="Tell me about cars",
        )

        system_text = context[0]["content"]
        self.assertIn("22:13 (10:13 PM)", system_text)
        self.assertIn("track-based vehicles", system_text)
        self.assertNotIn("rushed to sleep", system_text)
        self.assertNotIn("Resists bedtime", system_text)
        self.assertNotIn("Student One", system_text)
        self.assertLessEqual(len(context) - 1, MAX_CONTEXT_MESSAGES)
        self.assertEqual(context[-1]["content"], "Tell me about cars")

    def test_time_query_does_not_retrieve_sleep_memory(self):
        memory = self.make_memory()
        context = memory.build_context(
            "BASE PROMPT",
            "Friday, July 17, 2026; 22:13 (10:13 PM); Asia/Shanghai, UTC+8.",
            current_query="现在几点？",
        )
        self.assertNotIn("rushed to sleep", context[0]["content"])

    def test_new_session_clears_working_memory_not_long_term_memory(self):
        memory = self.make_memory()
        facts = list(memory._facts)
        profile = dict(memory._profile)

        memory.start_session()

        self.assertEqual(memory._messages, [])
        self.assertEqual(memory._facts, facts)
        self.assertEqual(memory._profile, profile)


if __name__ == "__main__":
    unittest.main()
