#!/usr/bin/env python3
"""
Migration v3: Add classmates table with all 40 students.
Reddy is student #39.
"""

import asyncio
import aiosqlite
import os

DB_PATH = os.path.join(os.path.dirname(__file__), "data", "reddy_memory.db")

CLASSMATES = [
    (1, "包墨一"),
    (2, "窦问哲"),
    (3, "高伯颜"),
    (4, "郭芃浩"),
    (5, "贺弘宣"),
    (6, "贺弘伊"),
    (7, "胡之茉"),
    (8, "季熙桐"),
    (9, "贾子诺"),
    (10, "姜泽宜"),
    (11, "李锦潼"),
    (12, "李舒颜"),
    (13, "连逸歆"),
    (14, "梁泽煦"),
    (15, "刘翰洋"),
    (16, "刘梓慕"),
    (17, "吕明泽"),
    (18, "马辰悦"),
    (19, "马梓玥"),
    (20, "莫择"),
    (21, "彭墨桐"),
    (22, "钱沐霖"),
    (23, "单乔恩"),
    (24, "孙琳清"),
    (25, "陶柒"),
    (26, "王兴泽"),
    (27, "王樱诺"),
    (28, "王正"),
    (29, "吴欣怡"),
    (30, "杨何彧灏"),
    (31, "杨天恩"),
    (32, "于飞洋"),
    (33, "于思齐"),
    (34, "曾歆珞"),
    (35, "张伯字"),
    (36, "张恩硕"),
    (37, "张伊朵"),
    (38, "赵冉佑"),
    (39, "赵瑞迪"),
    (40, "赵晞玥"),
]


async def migrate():
    db = await aiosqlite.connect(DB_PATH)
    await db.execute("PRAGMA journal_mode=WAL")

    await db.execute("""
        CREATE TABLE IF NOT EXISTS classmates (
            id INTEGER PRIMARY KEY,
            student_number INTEGER NOT NULL,
            name TEXT NOT NULL,
            created_at TEXT DEFAULT (datetime('now', '+8 hours'))
        )
    """)
    await db.commit()

    # Clear and repopulate
    await db.execute("DELETE FROM classmates")
    for sn, name in CLASSMATES:
        await db.execute(
            "INSERT INTO classmates (student_number, name) VALUES (?, ?)",
            (sn, name),
        )
    await db.commit()

    # Verify
    cursor = await db.execute("SELECT COUNT(*) FROM classmates")
    count = (await cursor.fetchone())[0]
    print(f"Classmates table: {count} students")

    cursor = await db.execute(
        "SELECT student_number, name FROM classmates ORDER BY student_number"
    )
    rows = await cursor.fetchall()
    for sn, name in rows:
        marker = " <-- Reddy" if sn == 39 else ""
        print(f"  #{sn} {name}{marker}")

    await db.close()
    print("\nMigration v3 complete.")


asyncio.run(migrate())
