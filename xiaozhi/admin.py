"""Admin HTTP server - conversation, system prompt, profile, memory management on port 7071."""

import json
import os
import asyncio
import logging
from urllib.parse import parse_qs, urlparse

from .ota_service import MAX_FIRMWARE_SIZE, OtaError, OtaService

logger = logging.getLogger("xz.admin")

# Paths
DB_PATH = os.path.join(os.path.dirname(__file__), "..", "data", "reddy_memory.db")
ENV_PATH = os.path.join(os.path.dirname(__file__), "..", ".env")
FIRMWARE_PATH = os.path.join(os.path.dirname(__file__), "..", "data", "firmware")

BASE_CSS = """
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:system-ui,sans-serif;background:#0f0f1a;color:#ccc;padding:16px}
nav{display:flex;gap:8px;margin-bottom:20px;flex-wrap:wrap}
nav a{color:#8888cc;text-decoration:none;padding:8px 16px;border:1px solid #333;border-radius:6px;font-size:14px}
nav a:hover,nav a.active{background:#1a1a2e;border-color:#8888cc}
h1{color:#8888cc;margin-bottom:16px;font-size:22px}
h2{color:#6677aa;margin:20px 0 12px;font-size:17px}
label{display:block;margin:12px 0 4px;color:#888;font-size:13px}
input,textarea,select{width:100%;padding:8px 12px;background:#1a1a2e;border:1px solid #333;border-radius:4px;color:#ccc;font-size:14px;font-family:inherit}
textarea{min-height:200px;resize:vertical;font-family:monospace;font-size:13px}
button{padding:8px 20px;background:#3366cc;color:#fff;border:none;border-radius:4px;cursor:pointer;font-size:14px;margin-top:12px}
button:hover{background:#4477dd}
button.danger{background:#993333} button.danger:hover{background:#aa4444}
.flash{padding:10px 16px;border-radius:4px;margin-bottom:12px;font-size:14px}
.flash.ok{background:#1a3a1a;color:#44cc88;border:1px solid #2a5a2a}
.flash.err{background:#3a1a1a;color:#cc4444;border:1px solid #5a2a2a}
.card{background:#12122a;border:1px solid #222;border-radius:8px;padding:16px;margin-bottom:16px}
.card h3{color:#8888cc;margin-bottom:8px}
.fact-row{display:flex;gap:8px;align-items:center;margin:4px 0}
.fact-row span{flex:1}
.fact-row .cat{color:#aa8844;font-size:12px;min-width:80px}
.fact-row .imp{color:#666;font-size:12px;min-width:30px}
.inline-form{display:flex;gap:8px}
.inline-form input{flex:1}
.inline-form button{padding:6px 12px;font-size:12px;margin-top:0}
.row{display:flex;gap:16px;flex-wrap:wrap}
.row>*{flex:1;min-width:300px}
table{width:100%;border-collapse:collapse}
th{background:#1a1a2e;color:#8888cc;padding:8px 12px;text-align:left;position:sticky;top:0}
td{padding:8px 12px;border-bottom:1px solid #1a1a2e;vertical-align:top;max-width:500px;word-break:break-all}
tr:hover{background:#1a1a30}
.user{color:#4488cc}.assistant{color:#44cc88}.system{color:#aa8844}
.time{color:#666;white-space:nowrap;font-size:12px}.tokens{color:#555;text-align:right;font-size:12px}
.controls{display:flex;gap:12px;align-items:center;margin-bottom:16px}
.controls button{padding:6px 16px;margin-top:0}
.controls button:disabled{opacity:0.4;cursor:default}
.controls span{color:#888}
.actions{display:flex;gap:8px}
.actions a,.actions button{padding:4px 10px;font-size:12px;margin-top:0;text-decoration:none;display:inline-block}

/* ---- mobile / phone view ---- */
@media (max-width: 768px) {
  body{padding:10px}
  nav{gap:4px;margin-bottom:14px}
  nav a{padding:8px 10px;font-size:12px;border-radius:4px}
  h1{font-size:18px;margin-bottom:10px}
  h2{font-size:15px;margin:14px 0 8px}
  input,textarea,select{font-size:16px;padding:10px 12px}
  button{font-size:14px;padding:10px 18px;width:100%}
  .controls{flex-wrap:wrap;gap:8px}
  .controls button{width:auto;padding:8px 14px;font-size:13px}
  .row{flex-direction:column;gap:12px}
  .row>*{min-width:100%}
  .card{padding:12px}
  .fact-row{flex-wrap:wrap;gap:6px;padding:4px 0;border-bottom:1px solid #1a1a2e}
  .fact-row .cat{min-width:60px;font-size:11px}
  .fact-row .imp{min-width:24px;font-size:11px}
  .fact-row span{font-size:13px}
  .fact-row button{margin-top:0;padding:4px 8px;font-size:11px;width:auto}
  .inline-form{flex-direction:column;gap:6px}
  .inline-form button{width:100%}
  /* scrollable table wrapper */
  .table-wrap{overflow-x:auto;-webkit-overflow-scrolling:touch;margin:0 -10px;padding:0 10px}
  th,td{padding:6px 8px;font-size:12px}
  th{position:static}
  td{max-width:200px}
  .time,.tokens{font-size:10px}
  textarea{min-height:150px;font-size:16px}
  pre{font-size:11px!important;max-height:250px!important}
}
@media (max-width: 400px) {
  body{padding:6px}
  nav a{padding:6px 8px;font-size:11px}
  h1{font-size:16px}
}
"""

NAV = """
<nav>
  <a href="/">Conversations</a>
  <a href="/prompt">System Prompt</a>
  <a href="/profile">Profile</a>
  <a href="/classmates">Classmates</a>
  <a href="/memory">Memory</a>
  <a href="/ota">OTA</a>
</nav>
"""

PAGE_HEADER = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Reddy Bot Admin</title><style>" + BASE_CSS + "</style></head><body>" + NAV + "<div id=\"content\">"
PAGE_FOOTER = "</div></body></html>"


class AdminServer:
    def __init__(self, db_path=None, host="0.0.0.0", port=7071):
        self._db_path = db_path or DB_PATH
        self._host = host
        self._port = port
        public_base_url = os.getenv(
            "OTA_PUBLIC_BASE_URL", f"http://127.0.0.1:{port}"
        )
        websocket_url = os.getenv(
            "OTA_WEBSOCKET_URL", "ws://127.0.0.1:7070/"
        )
        self._ota = OtaService(
            os.getenv("OTA_STORAGE_PATH", FIRMWARE_PATH),
            public_base_url,
            websocket_url,
            os.getenv("OTA_ADMIN_TOKEN", ""),
        )

    async def _handle_http(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        try:
            raw = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), timeout=30)
        except (asyncio.TimeoutError, ConnectionError, asyncio.IncompleteReadError):
            writer.close(); return

        request_text = raw.decode("utf-8", errors="replace")
        lines = request_text.split("\r\n")
        if not lines:
            writer.close(); return

        first_line = lines[0].split(" ", 2)
        method = first_line[0]
        path = first_line[1] if len(first_line) > 1 else "/"

        headers = {}
        for line in lines[1:]:
            if ":" in line:
                key, value = line.split(":", 1)
                headers[key.strip().lower()] = value.strip()

        try:
            content_length = int(headers.get("content-length", "0"))
        except ValueError:
            await self._send(
                writer, 400, '{"error":"invalid content length"}',
                "application/json",
            )
            return
        if content_length < 0 or content_length > MAX_FIRMWARE_SIZE:
            await self._send(
                writer, 413, '{"error":"request body too large"}',
                "application/json",
            )
            return
        try:
            body = (
                await asyncio.wait_for(
                    reader.readexactly(content_length), timeout=120
                )
                if content_length
                else b""
            )
        except (asyncio.TimeoutError, asyncio.IncompleteReadError):
            await self._send(
                writer, 400, '{"error":"incomplete request body"}',
                "application/json",
            )
            return

        parsed = urlparse(path)
        route = parsed.path

        if method == "GET":
            await self._route_get(route, parsed.query, headers, writer)
        elif method == "POST":
            await self._route_post(route, parsed.query, headers, body, writer)
        elif method == "OPTIONS":
            await self._send(writer, 204, "", "text/plain")
        else:
            await self._send(writer, 405, '{"error":"method not allowed"}', "application/json")

    # ---- GET routing ----
    async def _route_get(self, route: str, query: str, headers: dict, writer):
        if route == "/":
            await self._page_conversations(writer, query)
        elif route == "/prompt":
            await self._page_prompt(writer)
        elif route == "/profile":
            await self._page_profile(writer)
        elif route == "/memory":
            await self._page_memory(writer)
        elif route == "/classmates":
            await self._page_classmates(writer)
        elif route == "/ota":
            await self._page_ota(writer)
        elif route == "/ota/check":
            await self._api_ota_check(writer, headers, b"")
        elif route.startswith("/ota/firmware/"):
            await self._ota_firmware(
                writer, route[len("/ota/firmware/"):]
            )
        elif route == "/api/messages":
            await self._api_messages(writer, query)
        elif route == "/api/prompt":
            await self._api_get_prompt(writer)
        elif route == "/api/profile":
            await self._api_get_profile(writer)
        elif route == "/api/facts":
            await self._api_get_facts(writer)
        elif route == "/api/summary":
            await self._api_get_summary(writer)
        elif route == "/api/ota":
            await self._api_ota_status(writer)
        else:
            await self._send_html(writer, 404, "<h1>404 Not Found</h1>")

    # ---- POST routing ----
    async def _route_post(
        self, route: str, query: str, headers: dict, body: bytes, writer
    ):
        text_body = body.decode("utf-8", errors="replace")
        if route == "/api/prompt":
            await self._api_set_prompt(writer, text_body)
        elif route == "/api/profile":
            await self._api_set_profile(writer, text_body)
        elif route == "/api/facts":
            await self._api_set_facts(writer, text_body)
        elif route == "/api/facts/delete":
            await self._api_delete_fact(writer, text_body)
        elif route == "/api/summary":
            await self._api_set_summary(writer, text_body)
        elif route == "/api/classmates":
            await self._api_set_classmate(writer, text_body)
        elif route == "/api/classmates/delete":
            await self._api_delete_classmate(writer, text_body)
        elif route == "/ota/check":
            await self._api_ota_check(writer, headers, body)
        elif route == "/api/ota/firmware":
            await self._api_ota_publish(writer, query, headers, body)
        else:
            await self._send(writer, 404, '{"error":"not found"}', "application/json")

    # ==================== PAGES ====================

    async def _page_conversations(self, writer, query: str):
        html = PAGE_HEADER + """
        <h1>Conversation Log</h1>
        <div class="controls">
          <button onclick="prevPage()" id="prev">Prev</button>
          <span id="pageInfo">Page 1</span>
          <button onclick="nextPage()" id="next">Next</button>
          <span id="total"></span>
        </div>
        <div class="table-wrap"><table><thead><tr>
          <th style="width:60px">Role</th><th>Content</th>
          <th style="width:70px">Tokens</th><th style="width:160px">Time</th>
        </tr></thead><tbody id="tbody"></tbody></table></div>
        <div id="loading">Loading...</div>
        <script>
        let page=1, total=0;
        const perPage=100;
        const colors={user:'user',assistant:'assistant',system:'system'};
        async function load(p){
          document.getElementById('loading').style.display='block';
          document.getElementById('prev').disabled=true;
          document.getElementById('next').disabled=true;
          try{
            const r=await fetch('/api/messages?page='+p+'&per_page='+perPage);
            const d=await r.json();
            total=d.total; page=d.page;
            document.getElementById('pageInfo').textContent='Page '+page;
            document.getElementById('total').textContent='Total '+d.total_pages+' pages / '+total+' records';
            const tb=document.getElementById('tbody'); tb.innerHTML='';
            d.messages.forEach(m=>{
              const tr=document.createElement('tr');
              tr.innerHTML='<td class="'+colors[m.role]+'">'+esc(m.role)+'</td><td>'+esc(m.content||'(empty)')+'</td><td class="tokens">'+(m.tokens||0)+'</td><td class="time">'+esc(m.time||'')+'</td>';
              tb.appendChild(tr);
            });
            document.getElementById('prev').disabled=page<=1;
            document.getElementById('next').disabled=page>=d.total_pages;
          }catch(e){
            document.getElementById('tbody').innerHTML='<tr><td colspan="4">Load failed: '+esc(String(e))+'</td></tr>';
          }
          document.getElementById('loading').style.display='none';
        }
        function nextPage(){if(page<total)load(++page)}
        function prevPage(){if(page>1)load(--page)}
        function esc(s){const d=document.createElement('div');d.textContent=s;return d.innerHTML}
        load(1);
        </script>""" + PAGE_FOOTER
        await self._send_html(writer, 200, html)

    async def _page_prompt(self, writer):
        # Load current prompt
        import aiosqlite
        system_prompt = ""
        custom_prompt = os.getenv("SYSTEM_PROMPT", "")
        # Read from .env file for the actual value
        try:
            with open(ENV_PATH) as f:
                for line in f:
                    if line.startswith("SYSTEM_PROMPT="):
                        custom_prompt = line.split("=", 1)[1].strip().strip('"').strip("'")
        except Exception:
            pass
        # Also try reading from memory - there might be a stored prompt
        try:
            async with aiosqlite.connect(self._db_path) as db:
                await db.execute("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT)")
                await db.commit()
                cursor = await db.execute("SELECT value FROM settings WHERE key='system_prompt'")
                row = await cursor.fetchone()
                if row:
                    system_prompt = row[0]
        except Exception:
            pass

        current = system_prompt or custom_prompt
        escaped = current.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")

        html = PAGE_HEADER + f"""
        <h1>System Prompt</h1>
        <div id="flash"></div>
        <form id="promptForm" onsubmit="savePrompt(event)">
          <label>Current System Prompt (saved in DB, overrides .env)</label>
          <textarea id="promptText" name="prompt">{escaped}</textarea>
          <div style="display:flex;gap:8px;align-items:center;margin-top:12px">
            <button type="submit">Save to DB</button>
            <button type="button" class="danger" onclick="resetDefault()">Reset to Default</button>
          </div>
        </form>
        <h2>Default Prompt (code default, shown for reference)</h2>
        <pre style="background:#1a1a2e;padding:12px;border-radius:4px;font-size:12px;white-space:pre-wrap;max-height:400px;overflow:auto">""" + self._get_default_prompt().replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;") + """</pre>
        <script>
        function showFlash(msg,ok){{var f=document.getElementById('flash');f.className='flash '+(ok?'ok':'err');f.textContent=msg;setTimeout(()=>f.textContent='',3000)}}
        async function savePrompt(e){{
          e.preventDefault();
          var text=document.getElementById('promptText').value;
          try{{
            var r=await fetch('/api/prompt',{{method:'POST',body:text}});
            var d=await r.json();
            showFlash(d.ok?'Saved OK':'Error: '+d.error,d.ok);
          }}catch(x){{showFlash('Network error: '+x,false)}}
        }}
        async function resetDefault(){{
          if(!confirm('Reset system prompt to code default? This will delete the DB override.'))return;
          try{{
            var r=await fetch('/api/prompt',{{method:'POST',body:'__RESET__'}});
            var d=await r.json();
            if(d.ok){{document.getElementById('promptText').value=d.prompt;showFlash('Reset to default',true)}}
            else showFlash('Error: '+d.error,false);
          }}catch(x){{showFlash('Network error: '+x,false)}}
        }}
        </script>""" + PAGE_FOOTER
        await self._send_html(writer, 200, html)

    async def _page_profile(self, writer):
        import aiosqlite
        profile_json = "{}"
        try:
            async with aiosqlite.connect(self._db_path) as db:
                cursor = await db.execute("SELECT profile_json FROM user_profile WHERE id=1")
                row = await cursor.fetchone()
                if row:
                    profile_json = row[0]
        except Exception:
            pass

        profile_display = json.dumps(json.loads(profile_json) if profile_json else {}, indent=2, ensure_ascii=False)
        escaped = profile_display.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

        html = PAGE_HEADER + f"""
        <h1>User Profile</h1>
        <div id="flash"></div>
        <form onsubmit="saveProfile(event)">
          <label>Profile JSON</label>
          <textarea id="profileText" name="profile" style="min-height:400px">{escaped}</textarea>
          <button type="submit">Save Profile</button>
        </form>
        <script>
        function showFlash(msg,ok){{var f=document.getElementById('flash');f.className='flash '+(ok?'ok':'err');f.textContent=msg;setTimeout(()=>f.textContent='',3000)}}
        async function saveProfile(e){{
          e.preventDefault();
          var text=document.getElementById('profileText').value;
          try{{JSON.parse(text)}}catch(x){{showFlash('Invalid JSON: '+x,false);return}}
          try{{
            var r=await fetch('/api/profile',{{method:'POST',body:text}});
            var d=await r.json();
            showFlash(d.ok?'Saved OK':'Error: '+d.error,d.ok);
          }}catch(x){{showFlash('Network error: '+x,false)}}
        }}
        </script>""" + PAGE_FOOTER
        await self._send_html(writer, 200, html)

    async def _page_memory(self, writer):
        import aiosqlite
        facts = []
        summary = ""
        try:
            async with aiosqlite.connect(self._db_path) as db:
                cursor = await db.execute("SELECT content, category, importance FROM facts ORDER BY importance DESC")
                rows = await cursor.fetchall()
                facts = [{"content": r[0], "category": r[1], "importance": r[2]} for r in rows]
                cursor = await db.execute("SELECT content FROM session_summary WHERE id=1")
                row = await cursor.fetchone()
                if row:
                    summary = row[0]
        except Exception:
            pass

        facts_html = ""
        for i, f in enumerate(facts):
            fc = f["content"].replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")
            facts_html += f"""
            <div class="fact-row">
              <span class="cat">[{f['category']}]</span>
              <span class="imp">lvl{f['importance']}</span>
              <span>{fc}</span>
              <button class="danger" onclick="deleteFact('{fc[:50]}')" style="margin-top:0;padding:2px 8px;font-size:11px">X</button>
            </div>"""

        summary_esc = summary.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")

        html = PAGE_HEADER + f"""
        <h1>Memory Management</h1>
        <div id="flash"></div>

        <div class="row">
          <div class="card">
            <h2>Facts ({len(facts)} total)</h2>
            {facts_html}
            <div style="margin-top:16px;border-top:1px solid #222;padding-top:12px">
              <h3>Add Fact</h3>
              <form onsubmit="addFact(event)" class="inline-form">
                <input type="text" id="newFact" placeholder="Fact content..." required>
                <select id="newCat"><option>general</option><option>interest</option><option>personality</option><option>preference</option><option>event</option><option>family</option><option>other</option></select>
                <select id="newImp"><option>5</option><option>10</option><option>8</option><option>7</option><option>6</option><option>4</option><option>3</option></select>
                <button type="submit">Add</button>
              </form>
            </div>
          </div>

          <div class="card">
            <h2>Session Summary</h2>
            <form onsubmit="saveSummary(event)">
              <textarea id="summaryText" name="summary" style="min-height:150px">{summary_esc}</textarea>
              <button type="submit">Save Summary</button>
            </form>
          </div>
        </div>

        <script>
        function showFlash(msg,ok){{var f=document.getElementById('flash');f.className='flash '+(ok?'ok':'err');f.textContent=msg;setTimeout(()=>f.textContent='',3000)}}
        async function addFact(e){{
          e.preventDefault();
          var content=document.getElementById('newFact').value;
          var cat=document.getElementById('newCat').value;
          var imp=document.getElementById('newImp').value;
          try{{
            var r=await fetch('/api/facts',{{method:'POST',body:JSON.stringify({{content:content,category:cat,importance:parseInt(imp)}})}});
            var d=await r.json();
            showFlash(d.ok?'Added OK':'Error: '+d.error,d.ok);
            if(d.ok)setTimeout(()=>location.reload(),500);
          }}catch(x){{showFlash('Network error: '+x,false)}}
        }}
        async function deleteFact(content){{
          if(!confirm('Delete this fact?'))return;
          try{{
            var r=await fetch('/api/facts/delete',{{method:'POST',body:JSON.stringify({{content:content}})}});
            var d=await r.json();
            showFlash(d.ok?'Deleted OK':'Error: '+d.error,d.ok);
            if(d.ok)setTimeout(()=>location.reload(),500);
          }}catch(x){{showFlash('Network error: '+x,false)}}
        }}
        async function saveSummary(e){{
          e.preventDefault();
          var text=document.getElementById('summaryText').value;
          try{{
            var r=await fetch('/api/summary',{{method:'POST',body:text}});
            var d=await r.json();
            showFlash(d.ok?'Saved OK':'Error: '+d.error,d.ok);
          }}catch(x){{showFlash('Network error: '+x,false)}}
        }}
        </script>""" + PAGE_FOOTER
        await self._send_html(writer, 200, html)

    async def _page_classmates(self, writer):
        import aiosqlite
        classmates = []
        try:
            async with aiosqlite.connect(self._db_path) as db:
                await db.execute(
                    "CREATE TABLE IF NOT EXISTS classmates ("
                    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "  student_number INTEGER NOT NULL,"
                    "  name TEXT NOT NULL,"
                    "  created_at TEXT DEFAULT (datetime('now', '+8 hours'))"
                    ")"
                )
                await db.commit()
                cursor = await db.execute(
                    "SELECT student_number, name FROM classmates ORDER BY student_number"
                )
                rows = await cursor.fetchall()
                classmates = [{"student_number": r[0], "name": r[1]} for r in rows]
        except Exception:
            pass

        rows_html = ""
        for c in classmates:
            name_esc = c["name"].replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            marker = " (Reddy)" if c["student_number"] == 39 else ""
            rows_html += f"""
            <div class="fact-row">
              <span class="imp">#{c['student_number']}</span>
              <span>{name_esc}{marker}</span>
              <button class="danger" onclick="deleteClassmate({c['student_number']})" style="margin-top:0;padding:2px 8px;font-size:11px">X</button>
            </div>"""

        html = PAGE_HEADER + f"""
        <h1>Classmates ({len(classmates)} students)</h1>
        <div id="flash"></div>

        <div class="card">
          <h2>All Classmates (sorted by student number)</h2>
          {rows_html}
          <div style="margin-top:16px;border-top:1px solid #222;padding-top:12px">
            <h3>Add Classmate</h3>
            <form onsubmit="addClassmate(event)" class="inline-form">
              <input type="number" id="newSn" placeholder="Student #" required min="1" style="flex:0.5">
              <input type="text" id="newName" placeholder="Name" required>
              <button type="submit">Add</button>
            </form>
          </div>
        </div>

        <script>
        function showFlash(msg,ok){{var f=document.getElementById('flash');f.className='flash '+(ok?'ok':'err');f.textContent=msg;setTimeout(()=>f.textContent='',3000)}}
        async function addClassmate(e){{
          e.preventDefault();
          var sn=document.getElementById('newSn').value;
          var name=document.getElementById('newName').value;
          try{{
            var r=await fetch('/api/classmates',{{method:'POST',body:JSON.stringify({{student_number:parseInt(sn),name:name}})}});
            var d=await r.json();
            showFlash(d.ok?'Added OK':'Error: '+d.error,d.ok);
            if(d.ok)setTimeout(()=>location.reload(),500);
          }}catch(x){{showFlash('Network error: '+x,false)}}
        }}
        async function deleteClassmate(sn){{
          if(!confirm('Delete classmate #'+sn+'?'))return;
          try{{
            var r=await fetch('/api/classmates/delete',{{method:'POST',body:JSON.stringify({{student_number:sn}})}});
            var d=await r.json();
            showFlash(d.ok?'Deleted OK':'Error: '+d.error,d.ok);
            if(d.ok)setTimeout(()=>location.reload(),500);
          }}catch(x){{showFlash('Network error: '+x,false)}}
        }}
        </script>""" + PAGE_FOOTER
        await self._send_html(writer, 200, html)

    async def _page_ota(self, writer):
        html = PAGE_HEADER + """
        <h1>Firmware OTA</h1>
        <div id="flash"></div>
        <div class="row">
          <div class="card">
            <h3>Current release</h3>
            <pre id="status" style="white-space:pre-wrap;color:#91a5bb">Loading...</pre>
          </div>
          <div class="card">
            <h3>Safety model</h3>
            <p style="line-height:1.7;color:#8292a8">
              The server verifies the ESP image project and embedded version before
              publishing. Devices verify image size, SHA-256 and app metadata again,
              then boot from the inactive OTA partition with automatic rollback.
            </p>
          </div>
        </div>
        <div class="card">
          <h3>Publish firmware</h3>
          <form id="otaForm" onsubmit="publishFirmware(event)">
            <label>Firmware app image (.bin, up to 6 MiB)</label>
            <input id="firmware" type="file" accept=".bin,application/octet-stream" required>
            <div class="row">
              <div>
                <label>Version (must match embedded version)</label>
                <input id="version" value="2.3.0" required pattern="v?[0-9]+(\\.[0-9]+)*">
              </div>
              <div>
                <label>Board</label>
                <input id="board" value="esp32-s3-touch-amoled-1.8" required>
              </div>
            </div>
            <div class="row">
              <div>
                <label>Rollout percentage</label>
                <input id="rollout" type="number" value="100" min="0" max="100" required>
              </div>
              <div>
                <label>Minimum safe version (optional)</label>
                <input id="minVersion" placeholder="e.g. 2.3.0">
              </div>
            </div>
            <label>Release notes</label>
            <textarea id="notes" style="min-height:90px" maxlength="2000"></textarea>
            <label style="display:flex;gap:8px;align-items:center">
              <input id="force" type="checkbox" style="width:auto">
              Force install even when version is not newer
            </label>
            <label>OTA publish token (kept only in this browser tab)</label>
            <input id="token" type="password" autocomplete="off" required>
            <button id="publishButton" type="submit">Validate and publish</button>
          </form>
        </div>
        <script>
        const flash=document.getElementById('flash');
        function showFlash(message,ok){
          flash.className='flash '+(ok?'ok':'err');
          flash.textContent=message;
        }
        async function loadStatus(){
          try{
            const response=await fetch('/api/ota');
            const data=await response.json();
            document.getElementById('status').textContent=JSON.stringify(data,null,2);
          }catch(error){
            document.getElementById('status').textContent='Load failed: '+error;
          }
        }
        async function publishFirmware(event){
          event.preventDefault();
          const file=document.getElementById('firmware').files[0];
          const token=document.getElementById('token').value;
          sessionStorage.setItem('otaToken',token);
          const params=new URLSearchParams({
            version:document.getElementById('version').value,
            board:document.getElementById('board').value,
            rollout:document.getElementById('rollout').value,
            min_version:document.getElementById('minVersion').value,
            release_notes:document.getElementById('notes').value,
            force:document.getElementById('force').checked?'1':'0'
          });
          const button=document.getElementById('publishButton');
          button.disabled=true;
          button.textContent='Validating...';
          try{
            const response=await fetch('/api/ota/firmware?'+params.toString(),{
              method:'POST',
              headers:{'Content-Type':'application/octet-stream','X-OTA-Token':token},
              body:file
            });
            const data=await response.json();
            showFlash(data.ok?'Published '+data.manifest.version:(data.error||'Publish failed'),data.ok);
            if(data.ok)loadStatus();
          }catch(error){
            showFlash('Network error: '+error,false);
          }finally{
            button.disabled=false;
            button.textContent='Validate and publish';
          }
        }
        document.getElementById('token').value=sessionStorage.getItem('otaToken')||'';
        loadStatus();
        </script>""" + PAGE_FOOTER
        await self._send_html(writer, 200, html)

    # ==================== API ====================

    async def _api_messages(self, writer, query_string: str):
        params = parse_qs(query_string)
        page = int(params.get("page", ["1"])[0])
        per_page = min(int(params.get("per_page", ["100"])[0]), 500)

        import aiosqlite
        async with aiosqlite.connect(self._db_path) as db:
            cursor = await db.execute("SELECT COUNT(*) FROM messages")
            row = await cursor.fetchone()
            total = row[0] if row else 0

            offset = (page - 1) * per_page
            cursor = await db.execute(
                "SELECT role, content, token_count, created_at FROM messages ORDER BY id DESC LIMIT ? OFFSET ?",
                (per_page, offset),
            )
            rows = await cursor.fetchall()

        messages = [{
            "role": r[0], "content": r[1] or "",
            "tokens": r[2] or 0, "time": r[3] or "",
        } for r in rows]
        total_pages = max(1, (total + per_page - 1) // per_page)
        resp = {"page": page, "per_page": per_page, "total": total, "total_pages": total_pages, "messages": messages}
        await self._send(writer, 200, json.dumps(resp, ensure_ascii=False), "application/json; charset=utf-8")

    async def _api_get_prompt(self, writer):
        import aiosqlite
        try:
            async with aiosqlite.connect(self._db_path) as db:
                await db.execute("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT)")
                await db.commit()
                cursor = await db.execute("SELECT value FROM settings WHERE key='system_prompt'")
                row = await cursor.fetchone()
                prompt = row[0] if row else self._get_default_prompt()
        except Exception:
            prompt = self._get_default_prompt()
        await self._send(writer, 200, json.dumps({"prompt": prompt}), "application/json; charset=utf-8")

    async def _api_set_prompt(self, writer, body: str):
        import aiosqlite
        if body == "__RESET__":
            try:
                async with aiosqlite.connect(self._db_path) as db:
                    await db.execute("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT)")
                    await db.commit()
                    await db.execute("DELETE FROM settings WHERE key='system_prompt'")
                    await db.commit()
                await self._send(writer, 200, json.dumps({"ok": True, "prompt": self._get_default_prompt()}), "application/json; charset=utf-8")
            except Exception as e:
                await self._send(writer, 500, json.dumps({"ok": False, "error": str(e)}), "application/json; charset=utf-8")
        else:
            try:
                async with aiosqlite.connect(self._db_path) as db:
                    await db.execute("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT)")
                    await db.commit()
                    await db.execute("INSERT OR REPLACE INTO settings (key, value) VALUES ('system_prompt', ?)", (body,))
                    await db.commit()
                await self._send(writer, 200, json.dumps({"ok": True}), "application/json; charset=utf-8")
            except Exception as e:
                await self._send(writer, 500, json.dumps({"ok": False, "error": str(e)}), "application/json; charset=utf-8")

    async def _api_get_profile(self, writer):
        import aiosqlite
        try:
            async with aiosqlite.connect(self._db_path) as db:
                cursor = await db.execute("SELECT profile_json FROM user_profile WHERE id=1")
                row = await cursor.fetchone()
                profile = json.loads(row[0]) if row else {}
        except Exception:
            profile = {}
        await self._send(writer, 200, json.dumps(profile, ensure_ascii=False), "application/json; charset=utf-8")

    async def _api_set_profile(self, writer, body: str):
        import aiosqlite
        try:
            json.loads(body)  # validate
            async with aiosqlite.connect(self._db_path) as db:
                await db.execute("DELETE FROM user_profile")
                await db.execute("INSERT INTO user_profile (id, profile_json) VALUES (1, ?)", (body,))
                await db.commit()
            await self._send(writer, 200, json.dumps({"ok": True}), "application/json; charset=utf-8")
        except json.JSONDecodeError as e:
            await self._send(writer, 400, json.dumps({"ok": False, "error": f"Invalid JSON: {e}"}), "application/json; charset=utf-8")
        except Exception as e:
            await self._send(writer, 500, json.dumps({"ok": False, "error": str(e)}), "application/json; charset=utf-8")

    async def _api_get_facts(self, writer):
        import aiosqlite
        try:
            async with aiosqlite.connect(self._db_path) as db:
                cursor = await db.execute("SELECT content, category, importance FROM facts ORDER BY importance DESC")
                rows = await cursor.fetchall()
                facts = [{"content": r[0], "category": r[1], "importance": r[2]} for r in rows]
        except Exception:
            facts = []
        await self._send(writer, 200, json.dumps(facts, ensure_ascii=False), "application/json; charset=utf-8")

    async def _api_set_facts(self, writer, body: str):
        import aiosqlite
        try:
            fact = json.loads(body)
            content = fact.get("content", "").strip()
            if not content:
                raise ValueError("Empty content")
            async with aiosqlite.connect(self._db_path) as db:
                await db.execute(
                    "INSERT INTO facts (content, category, importance) VALUES (?, ?, ?)",
                    (content, fact.get("category", "general"), int(fact.get("importance", 5))),
                )
                await db.commit()
            await self._send(writer, 200, json.dumps({"ok": True}), "application/json; charset=utf-8")
        except Exception as e:
            await self._send(writer, 500, json.dumps({"ok": False, "error": str(e)}), "application/json; charset=utf-8")

    async def _api_delete_fact(self, writer, body: str):
        import aiosqlite
        try:
            data = json.loads(body)
            content = data.get("content", "")
            async with aiosqlite.connect(self._db_path) as db:
                await db.execute("DELETE FROM facts WHERE content = ?", (content,))
                await db.commit()
            await self._send(writer, 200, json.dumps({"ok": True}), "application/json; charset=utf-8")
        except Exception as e:
            await self._send(writer, 500, json.dumps({"ok": False, "error": str(e)}), "application/json; charset=utf-8")

    async def _api_set_classmate(self, writer, body: str):
        import aiosqlite
        try:
            data = json.loads(body)
            sn = int(data.get("student_number", 0))
            name = data.get("name", "").strip()
            if not sn or not name:
                raise ValueError("student_number and name required")
            async with aiosqlite.connect(self._db_path) as db:
                await db.execute(
                    "CREATE TABLE IF NOT EXISTS classmates ("
                    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "  student_number INTEGER NOT NULL,"
                    "  name TEXT NOT NULL,"
                    "  created_at TEXT DEFAULT (datetime('now', '+8 hours'))"
                    ")"
                )
                # Upsert: delete existing then insert
                await db.execute("DELETE FROM classmates WHERE student_number = ?", (sn,))
                await db.execute(
                    "INSERT INTO classmates (student_number, name) VALUES (?, ?)",
                    (sn, name),
                )
                await db.commit()
            await self._send(writer, 200, json.dumps({"ok": True}), "application/json; charset=utf-8")
        except Exception as e:
            await self._send(writer, 500, json.dumps({"ok": False, "error": str(e)}), "application/json; charset=utf-8")

    async def _api_delete_classmate(self, writer, body: str):
        import aiosqlite
        try:
            data = json.loads(body)
            sn = int(data.get("student_number", 0))
            if not sn:
                raise ValueError("student_number required")
            async with aiosqlite.connect(self._db_path) as db:
                await db.execute("DELETE FROM classmates WHERE student_number = ?", (sn,))
                await db.commit()
            await self._send(writer, 200, json.dumps({"ok": True}), "application/json; charset=utf-8")
        except Exception as e:
            await self._send(writer, 500, json.dumps({"ok": False, "error": str(e)}), "application/json; charset=utf-8")

    async def _api_get_summary(self, writer):
        import aiosqlite
        try:
            async with aiosqlite.connect(self._db_path) as db:
                cursor = await db.execute("SELECT content FROM session_summary WHERE id=1")
                row = await cursor.fetchone()
                summary = row[0] if row else ""
        except Exception:
            summary = ""
        await self._send(writer, 200, json.dumps({"summary": summary}), "application/json; charset=utf-8")

    async def _api_set_summary(self, writer, body: str):
        import aiosqlite
        try:
            async with aiosqlite.connect(self._db_path) as db:
                await db.execute("DELETE FROM session_summary")
                await db.execute(
                    "INSERT INTO session_summary (id, content, token_count) VALUES (1, ?, ?)",
                    (body, len(body) // 3),
                )
                await db.commit()
            await self._send(writer, 200, json.dumps({"ok": True}), "application/json; charset=utf-8")
        except Exception as e:
            await self._send(writer, 500, json.dumps({"ok": False, "error": str(e)}), "application/json; charset=utf-8")

    async def _api_ota_check(self, writer, headers: dict, body: bytes):
        payload = {}
        if body:
            try:
                payload = json.loads(body.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                payload = {}

        application = payload.get("application", {})
        current_version = str(application.get("version", "")).strip()
        user_agent = headers.get("user-agent", "")
        board = ""
        if "/" in user_agent:
            ua_board, ua_version = user_agent.rsplit("/", 1)
            board = ua_board.strip()
            current_version = current_version or ua_version.strip()
        device_id = (
            headers.get("device-id")
            or str(payload.get("mac_address", ""))
            or headers.get("client-id", "")
        )
        response = self._ota.check(
            device_id=device_id,
            current_version=current_version,
            board=board,
        )
        await self._send(
            writer,
            200,
            json.dumps(response, ensure_ascii=False, separators=(",", ":")),
            "application/json; charset=utf-8",
        )

    async def _api_ota_status(self, writer):
        try:
            response = self._ota.status()
            await self._send(
                writer,
                200,
                json.dumps(response, ensure_ascii=False),
                "application/json; charset=utf-8",
            )
        except OtaError as exc:
            await self._send(
                writer,
                500,
                json.dumps({"error": str(exc)}),
                "application/json; charset=utf-8",
            )

    async def _api_ota_publish(
        self, writer, query: str, headers: dict, body: bytes
    ):
        if not self._ota.is_authorized(headers.get("x-ota-token", "")):
            await self._send(
                writer,
                401,
                json.dumps({"ok": False, "error": "invalid OTA publish token"}),
                "application/json; charset=utf-8",
            )
            return

        params = parse_qs(query, keep_blank_values=True)
        get = lambda name, default="": params.get(name, [default])[0]
        try:
            manifest = self._ota.publish(
                body,
                version=get("version"),
                board=get("board"),
                force=get("force", "0").lower() in {"1", "true", "yes"},
                rollout=int(get("rollout", "100")),
                min_version=get("min_version"),
                release_notes=get("release_notes"),
            )
            await self._send(
                writer,
                200,
                json.dumps(
                    {"ok": True, "manifest": manifest.__dict__},
                    ensure_ascii=False,
                ),
                "application/json; charset=utf-8",
            )
        except (OtaError, ValueError) as exc:
            await self._send(
                writer,
                400,
                json.dumps({"ok": False, "error": str(exc)}),
                "application/json; charset=utf-8",
            )
        except OSError:
            logger.exception("Failed to publish OTA firmware")
            await self._send(
                writer,
                500,
                json.dumps({"ok": False, "error": "failed to store firmware"}),
                "application/json; charset=utf-8",
            )

    async def _ota_firmware(self, writer, filename: str):
        try:
            path = self._ota.get_firmware_path(filename)
        except OtaError:
            path = None
        if path is None:
            await self._send(
                writer, 404, '{"error":"firmware not found"}',
                "application/json",
            )
            return
        await self._send_file(writer, path)

    # ==================== Helpers ====================

    def _get_default_prompt(self):
        # Keep the admin reset action in sync with the runtime default.
        from .agent import DEFAULT_SYSTEM_PROMPT
        return DEFAULT_SYSTEM_PROMPT


    @staticmethod
    async def _send_html(writer, status: int, html: str):
        body = html.encode("utf-8")
        writer.write(
            f"HTTP/1.1 {status} OK\r\n"
            f"Content-Type: text/html; charset=utf-8\r\n"
            f"Content-Length: {len(body)}\r\n"
            f"Connection: close\r\n\r\n".encode("utf-8") + body
        )
        await writer.drain()
        writer.close()

    @staticmethod
    async def _send(writer, status: int, body: str, content_type: str):
        data = body.encode("utf-8")
        writer.write(
            f"HTTP/1.1 {status} OK\r\n"
            f"Content-Type: {content_type}\r\n"
            f"Content-Length: {len(data)}\r\n"
            f"Access-Control-Allow-Origin: *\r\n"
            f"Connection: close\r\n\r\n".encode("utf-8") + data
        )
        await writer.drain()
        writer.close()

    @staticmethod
    async def _send_file(writer, path):
        size = path.stat().st_size
        writer.write(
            f"HTTP/1.1 200 OK\r\n"
            f"Content-Type: application/octet-stream\r\n"
            f"Content-Length: {size}\r\n"
            f"Cache-Control: public, max-age=31536000, immutable\r\n"
            f"Connection: close\r\n\r\n".encode("utf-8")
        )
        with path.open("rb") as handle:
            while True:
                chunk = handle.read(64 * 1024)
                if not chunk:
                    break
                writer.write(chunk)
                await writer.drain()
        writer.close()

    async def start(self):
        server = await asyncio.start_server(self._handle_http, self._host, self._port)
        logger.info(f"Admin HTTP on http://{self._host}:{self._port}")
        async with server:
            await server.serve_forever()
