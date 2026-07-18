#!/usr/bin/env python3
"""Apply the focused prompt and memory policy to the live Reddy database."""

import json
import sqlite3
from datetime import date

from xiaozhi.agent import DEFAULT_SYSTEM_PROMPT


DB_PATH = "./data/reddy_memory.db"

STABLE_INTERESTS = [
    "Cars, trains, buses, and track-based vehicles",
    "Learning piano",
    "Weekly English class",
    "Basketball, swimming, rollerblading, and art",
    "Singing Twinkle Twinkle Little Star",
    "Word-guessing and letter-guessing games",
    "Counting games",
    "Role-play and imaginative games",
    "Airplanes and travel",
]


def clean_profile(profile: dict) -> dict:
    important_dates = []
    for item in profile.get("important_dates", []):
        if not isinstance(item, dict):
            continue
        event = str(item.get("event", "")).strip()
        value = str(item.get("date", "")).strip()
        if not event or not value:
            continue
        # Birthdays are recurring; other dated events should not survive after
        # they have passed.
        if "birthday" not in event.lower():
            try:
                event_date = date.fromisoformat(value[:10])
                if event_date < date.today():
                    continue
            except ValueError:
                pass
        important_dates.append({"event": event, "date": value})

    return {
        "name": "Reddy",
        "chinese_name": profile.get("chinese_name", "瑞迪"),
        "age": profile.get("age", 6),
        "grade": profile.get("grade", "1st grade"),
        "interests": STABLE_INTERESTS,
        "dislikes": [
            "Being rushed to sleep",
            "Being morally judged or lectured",
            "Hot things",
            "Dinosaurs",
        ],
        "important_dates": important_dates,
        "preferences": {
            "language": "Reddy may speak Chinese; Xiaolan answers in simple English",
            "interaction": "Playful and concise, without moralizing or unsolicited bedtime reminders",
        },
        "notes": (
            "Use interests only when they match the current topic. Encourage piano "
            "or English only when Reddy brings them up. Never volunteer sensitive "
            "family information."
        ),
        "last_updated": date.today().isoformat(),
    }


def migrate(db_path: str = DB_PATH):
    db = sqlite3.connect(db_path)
    try:
        db.execute("BEGIN IMMEDIATE")
        db.execute(
            "CREATE TABLE IF NOT EXISTS settings "
            "(key TEXT PRIMARY KEY, value TEXT)"
        )

        row = db.execute(
            "SELECT profile_json FROM user_profile WHERE id=1"
        ).fetchone()
        old_profile = json.loads(row[0]) if row else {}
        new_profile = clean_profile(old_profile)
        db.execute(
            "INSERT OR REPLACE INTO user_profile (id, profile_json) VALUES (1, ?)",
            (json.dumps(new_profile, ensure_ascii=False),),
        )

        db.execute(
            "UPDATE facts SET content=?, category='preference', importance=7 "
            "WHERE lower(content) LIKE '%rushed to sleep%'",
            (
                "When sleep is already the current topic, Reddy prefers gentle, "
                "non-pushy replies.",
            ),
        )
        db.execute("DELETE FROM session_summary")
        db.execute(
            "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)",
            ("system_prompt", DEFAULT_SYSTEM_PROMPT),
        )
        db.execute(
            "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)",
            ("memory_policy_version", "focused-v4"),
        )
        db.commit()

        counts = {
            table: db.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
            for table in ("messages", "facts", "user_profile", "session_summary")
        }
        print(json.dumps({"ok": True, "counts": counts}, ensure_ascii=False))
    except Exception:
        db.rollback()
        raise
    finally:
        db.close()


if __name__ == "__main__":
    migrate()
