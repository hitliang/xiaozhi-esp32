#!/usr/bin/env python3
"""
Clean rebuild of Reddy Bot database.
- Rebuild facts from profile (high-quality, English)
- Clean up profile (remove garbage, English only)
- Delete old messages (all Chinese, will pollute LLM context)
"""

import asyncio
import json
import aiosqlite

async def rebuild():
    db = await aiosqlite.connect('/root/reddy_bot/data/reddy_memory.db')
    await db.execute("PRAGMA journal_mode=WAL")
    
    # =====================
    # 1. Delete all old messages (Chinese, will pollute context)
    # =====================
    print("--- Clearing old messages ---")
    await db.execute("DELETE FROM messages")
    await db.commit()
    print("All messages cleared.")
    
    # =====================
    # 2. Delete all old facts (low quality)
    # =====================
    print("\n--- Clearing old facts ---")
    await db.execute("DELETE FROM facts")
    await db.commit()
    print("All facts cleared.")
    
    # =====================
    # 3. Rebuild profile (clean, English)
    # =====================
    print("\n--- Rebuilding profile ---")
    
    new_profile = {
        "name": "Reddy",
        "chinese_name": "赵瑞迪",
        "age": 6,
        "grade": "1st grade",
        "birthday": "2019-07-07",
        "interests": [
            "Loves vehicle toys: cars, trains, buses, especially track-based vehicles",
            "Learning piano (over half a year, one lesson per week)",
            "Weekly English class",
            "Watched Peppa Pig (English version) and Okido",
            "Has tried basketball, swimming, rollerblading, and art classes",
            "Likes singing 'Twinkle Twinkle Little Star'",
            "Likes word-guessing games and letter-guessing games",
            "Likes pretending to go to the underwater world",
            "Likes mimicking animal movements (monkey, duck, cat, etc.)",
            "Likes pretending to be 'Boss Reddy'"
        ],
        "dislikes": [
            "Doesn't like being rushed to sleep",
            "Doesn't like being morally judged"
        ],
        "important_dates": [
            {"event": "Birthday", "date": "2019-07-07"},
            {"event": "Starting 2nd grade", "date": "2026-09-01"}
        ],
        "preferences": {
            "language": "Prefers English, occasionally accepts some Chinese",
            "tone": "Warm, encouraging, playful, avoids moralizing"
        },
        "family": {
            "dad": "Born 1989, takes him to piano practice",
            "mom": "Born 1989, takes him to basketball class",
            "grandma": "Born 1963, picks him up from school and plays games with him",
            "note": "Grandpa passed away, do not mention him"
        },
        "daily_routine": "Dad takes him to piano practice after school, mom takes him to basketball class",
        "assistant_name": "小智",
        "assistant_persona": "Cheerful and cute sister figure",
        "notes": "Avoid mentioning grandpa. Likes encouragement, especially in piano and English. Very interested in cars. Scored 52 on an English test, may need encouragement. Likes word games (finding characters in Chinese). Shows playfulness and likes to tease. Named the assistant Jenny but also calls it Xiaozhi. Gets impatient about bedtime. Likes role-play games (underwater world, animals). Likes being called Boss Reddy.",
        "last_updated": "2026-05-10"
    }
    
    await db.execute(
        "UPDATE user_profile SET profile_json=? WHERE id=1",
        (json.dumps(new_profile, ensure_ascii=False),)
    )
    await db.commit()
    print("Profile rebuilt.")
    
    # =====================
    # 4. Create high-quality facts from profile
    # =====================
    print("\n--- Creating new facts ---")
    
    facts = [
        {"content": "Reddy is a 6-year-old boy", "category": "family", "importance": 10},
        {"content": "Reddy loves cars, trains, buses, and track-based vehicles", "category": "interest", "importance": 9},
        {"content": "Reddy is learning piano, has been studying for over half a year", "category": "interest", "importance": 8},
        {"content": "Reddy takes weekly English class", "category": "interest", "importance": 7},
        {"content": "Reddy has tried basketball, swimming, rollerblading, and art", "category": "interest", "importance": 6},
        {"content": "Reddy likes singing Twinkle Twinkle Little Star", "category": "interest", "importance": 5},
        {"content": "Reddy likes word-guessing and letter-guessing games", "category": "interest", "importance": 5},
        {"content": "Reddy likes pretending to go to the underwater world", "category": "interest", "importance": 4},
        {"content": "Reddy likes mimicking animal movements", "category": "interest", "importance": 4},
        {"content": "Reddy does not like being rushed to sleep", "category": "preference", "importance": 7},
        {"content": "Reddy does not like being morally judged", "category": "preference", "importance": 6},
        {"content": "Reddy prefers to think for himself rather than being given answers directly", "category": "personality", "importance": 7},
        {"content": "Reddy shows honesty when learning new things", "category": "personality", "importance": 5},
        {"content": "Reddy is playful, likes to tease, and enjoys being called Boss Reddy", "category": "personality", "importance": 5},
        {"content": "Reddy scored 52 on an English test and may need encouragement", "category": "event", "importance": 6},
        {"content": "Reddy watched Peppa Pig (English) and Okido", "category": "interest", "importance": 4},
        {"content": "Dad takes Reddy to piano practice, mom takes him to basketball", "category": "family", "importance": 6},
        {"content": "Grandma picks Reddy up from school and plays games with him", "category": "family", "importance": 5},
        {"content": "Grandpa passed away - do not mention him", "category": "family", "importance": 10},
        {"content": "Reddy's birthday is July 7, 2019", "category": "event", "importance": 8},
        {"content": "Reddy will start 2nd grade on September 1, 2026", "category": "event", "importance": 6},
    ]
    
    for f in facts:
        await db.execute(
            "INSERT INTO facts (content, category, importance) VALUES (?, ?, ?)",
            (f["content"], f["category"], f["importance"])
        )
    await db.commit()
    print(f"Created {len(facts)} high-quality facts.")
    
    # =====================
    # 5. Rebuild classmates
    # =====================
    print("\n--- Rebuilding classmates ---")
    await db.execute(
        "CREATE TABLE IF NOT EXISTS classmates ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  student_number INTEGER NOT NULL,"
        "  name TEXT NOT NULL,"
        "  created_at TEXT DEFAULT (datetime('now', '+8 hours'))"
        ")"
    )
    await db.execute("DELETE FROM classmates")

    classmates = [
        (1, "包墨一"), (2, "窦问哲"), (3, "高伯颜"), (4, "郭芃浩"),
        (5, "贺弘宣"), (6, "贺弘伊"), (7, "胡之茉"), (8, "季熙桐"),
        (9, "贾子诺"), (10, "姜泽宜"), (11, "李锦潼"), (12, "李舒颜"),
        (13, "连逸歆"), (14, "梁泽煦"), (15, "刘翰洋"), (16, "刘梓慕"),
        (17, "吕明泽"), (18, "马辰悦"), (19, "马梓玥"), (20, "莫择"),
        (21, "彭墨桐"), (22, "钱沐霖"), (23, "单乔恩"), (24, "孙琳清"),
        (25, "陶柒"), (26, "王兴泽"), (27, "王樱诺"), (28, "王正"),
        (29, "吴欣怡"), (30, "杨何彧灏"), (31, "杨天恩"), (32, "于飞洋"),
        (33, "于思齐"), (34, "曾歆珞"), (35, "张伯字"), (36, "张恩硕"),
        (37, "张伊朵"), (38, "赵冉佑"), (39, "赵瑞迪"), (40, "赵晞玥"),
    ]
    for sn, name in classmates:
        await db.execute(
            "INSERT INTO classmates (student_number, name) VALUES (?, ?)",
            (sn, name),
        )
    await db.commit()
    print(f"Created {len(classmates)} classmates.")

    # =====================
    # 6. Verify
    # =====================
    print("\n--- Verification ---")
    c = await db.execute("SELECT COUNT(*) FROM facts")
    print(f"Facts: {(await c.fetchone())[0]}")
    c = await db.execute("SELECT COUNT(*) FROM messages")
    print(f"Messages: {(await c.fetchone())[0]}")
    c = await db.execute("SELECT COUNT(*) FROM classmates")
    print(f"Classmates: {(await c.fetchone())[0]}")
    c = await db.execute("SELECT profile_json FROM user_profile WHERE id=1")
    row = await c.fetchone()
    p = json.loads(row[0])
    print(f"Profile name: {p.get('name')}")
    print(f"Profile grade: {p.get('grade')}")
    print(f"Profile interests: {len(p.get('interests', []))}")
    
    # Check for any remaining Chinese in profile
    def check_chinese(obj, path=""):
        if isinstance(obj, str):
            if any('\u4e00' <= c <= '\u9fff' for c in obj):
                print(f"  Chinese found: {path} = {obj[:50]}")
        elif isinstance(obj, dict):
            for k, v in obj.items():
                check_chinese(v, f"{path}.{k}")
        elif isinstance(obj, list):
            for i, v in enumerate(obj):
                check_chinese(v, f"{path}[{i}]")
    
    check_chinese(p, "profile")
    
    await db.close()
    print("\n=== Rebuild Complete ===")

asyncio.run(rebuild())
