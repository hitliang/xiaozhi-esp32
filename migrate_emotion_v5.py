"""Install the rich animated-face prompt without touching conversation memory."""

import os
import sqlite3

from xiaozhi.agent import DEFAULT_SYSTEM_PROMPT


def migrate() -> None:
    db_path = os.getenv("MEMORY_DB_PATH", "./data/reddy_memory.db")
    db = sqlite3.connect(db_path)
    try:
        db.execute(
            "CREATE TABLE IF NOT EXISTS settings "
            "(key TEXT PRIMARY KEY, value TEXT)"
        )
        db.execute(
            "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)",
            ("system_prompt", DEFAULT_SYSTEM_PROMPT),
        )
        db.execute(
            "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)",
            ("emotion_policy_version", "rich-face-v5"),
        )
        db.commit()
        print("Installed rich-face-v5 prompt; memories were left unchanged.")
    finally:
        db.close()


if __name__ == "__main__":
    migrate()
