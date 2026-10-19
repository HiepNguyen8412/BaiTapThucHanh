# Tích hợp CLI AI vào chương trình: Claude Code, Gemini CLI, Antigravity CLI, Codex CLI

> **Cập nhật:** 13/10/2026 · **Dự án:** Bot Telegram "phòng ban AI" (`C:\Users\Admin\Desktop\Bot`)
> **Mục đích:** ghi lại chính xác cách chương trình gọi từng CLI ở chế độ chạy ngầm (không tương tác): lệnh, cờ, định dạng vào/ra, lỗi thường gặp và cách xử lý. Mọi thông tin dưới đây đều đã **chạy thử thật trên máy này**, trừ những chỗ ghi rõ "chưa thử".

---

## Mục lục

1. [Môi trường và phiên bản đã kiểm chứng](#1-môi-trường-và-phiên-bản-đã-kiểm-chứng)
2. [Nguyên tắc chung khi điều khiển CLI từ chương trình](#2-nguyên-tắc-chung-khi-điều-khiển-cli-từ-chương-trình)
3. [Claude Code CLI 2.1.216](#3-claude-code-cli-21216)
4. [Gemini CLI 0.63.0 (không dùng được với tài khoản cá nhân)](#4-gemini-cli-0630)
5. [Antigravity CLI `agy` 1.3.x: cách dùng Gemini đang chạy được](#5-antigravity-cli-agy-13x)
6. [Antigravity IDE nội bộ: `agentapi` (ghi chép nghiên cứu, không dùng)](#6-antigravity-ide-nội-bộ-agentapi)
7. [Codex CLI 0.162.1](#7-codex-cli-01621)
8. [Bot áp dụng thế nào (bản đồ code)](#8-bot-áp-dụng-thế-nào-bản-đồ-code)
9. [Ví dụ tối giản để tự làm lại](#9-ví-dụ-tối-giản-để-tự-làm-lại)
10. [Xử lý sự cố](#10-xử-lý-sự-cố)
11. [Checklist khi thêm một CLI mới](#11-checklist-khi-thêm-một-cli-mới)
12. [Bảo mật và điều khoản](#12-bảo-mật-và-điều-khoản)

---

## 1. Môi trường và phiên bản đã kiểm chứng

### 1.1 Máy và công cụ nền

| Thành phần | Phiên bản | Ghi chú |
|---|---|---|
| Windows | 11 Home, build 10.0.22631.6199 | Mọi đường dẫn trong tài liệu là Windows |
| Python | 3.14.3 | Bot chạy trong venv `C:\Users\Admin\Desktop\Bot\.venv` |
| python-telegram-bot | 22.8 | |
| python-dotenv | 1.1.1 | |
| playwright (Python) | 1.55.0 | Dùng Edge có sẵn (`channel="msedge"`), không tải trình duyệt |
| httpx | 0.28.1 | Phụ thuộc của python-telegram-bot |
| Node.js / npm | v24.14.1 / 11.11.0 | Cần cho Claude Code, Gemini CLI, Codex CLI |
| Git | 2.53.0.windows.2 | Bot commit sau mỗi lượt agent |
| Microsoft Edge | 153.0.4234.48 | Để chụp màn hình giao diện |

### 1.2 Các CLI AI

| CLI | Phiên bản | Cài bằng | File chạy | Tình trạng |
|---|---|---|---|---|
| **Claude Code** | **2.1.216** | `npm i -g @anthropic-ai/claude-code` | `%APPDATA%\npm\claude.cmd` → `node_modules\@anthropic-ai\claude-code\bin\claude.exe` | ✅ Chạy tốt, dùng gói Claude đang đăng nhập |
| **Gemini CLI** | **0.63.0** | `npm i -g @google/gemini-cli` | `%APPDATA%\npm\gemini.cmd` | ❌ Tài khoản Google cá nhân bị chặn (xem mục 4.5) |
| **Antigravity CLI** (`agy`) | **1.3.3** khi cài, tự cập nhật lên **1.3.4** | `winget install --id Google.AntigravityCLI -e --source winget` | `%LOCALAPPDATA%\Microsoft\WinGet\Links\agy.exe` | ✅ Chạy tốt: Gemini 3.8 Flash, Claude 4.6, GPT-OSS |
| **Codex CLI** | **0.162.1** | `npm i -g @openai/codex` | `%APPDATA%\npm\codex.cmd` | ⚠️ Chạy được, nhưng chưa đăng nhập (401) |
| Antigravity IDE | 1.107.0 | (có sẵn) | `%LOCALAPPDATA%\Programs\Antigravity IDE\` | Chỉ dùng để nghiên cứu (mục 6) |

> Kiểm tra lại phiên bản: `claude --version`, `gemini --version`, `agy --version`, `codex --version`.
> Gói winget liên quan: `Google.AntigravityCLI` 1.3.3, `Google.Antigravity` 2.23.0 (app có Agent Manager), `Google.AntigravityIDE` 2.5.5.

### 1.3 Model đang dùng trong bot (13/10/2026)

| Nhân viên | Nền tảng | Model |
|---|---|---|
| Quản lý | Antigravity | `gemini-3.8-flash-high` |
| Tester | Antigravity | `gemini-3.8-flash-high` |
| Developer | Claude | `sonnet` |
| BA | Claude | `sonnet` |

---

## 2. Nguyên tắc chung khi điều khiển CLI từ chương trình

### 2.1 Luôn chạy chế độ không tương tác

Mỗi lượt làm việc là **một tiến trình CLI**: gửi prompt, chờ câu trả lời cuối, rồi tiến trình tự thoát. Không mở giao diện TUI.

### 2.2 Truyền prompt qua **stdin**, không qua tham số dòng lệnh

Lý do bắt buộc trên Windows:

1. **Giới hạn độ dài dòng lệnh** là 32.767 ký tự. Prompt của bot (luật, danh sách đội, `STATE.md`, lịch sử trò chuyện) dễ vượt 10.000 ký tự.
2. **Các CLI cài bằng npm là file `.cmd`**, nên khi chạy chúng đi qua `cmd.exe`. Các ký tự `& | % ^ " < >` và dấu xuống dòng trong prompt sẽ bị `cmd.exe` hiểu sai, thậm chí **có thể bị chèn lệnh** (lỗ hổng kiểu "BatBadBut").
3. **Tiếng Việt UTF-8** qua tham số dễ hỏng do bảng mã console. Ghi vào stdin dưới dạng `prompt.encode("utf-8")` thì an toàn.

Vì vậy tham số dòng lệnh **chỉ chứa giá trị đã kiểm tra**:

```python
SAFE_MODEL = re.compile(r"^[A-Za-z0-9._:/\[\]-]{1,80}$")   # tên model
SESSION_ID = re.compile(r"^[0-9a-fA-F-]{8,64}$")            # mã phiên để mở lại
```

Cách đưa prompt qua stdin khác nhau tuỳ CLI:

| CLI | Cách đưa prompt qua stdin |
|---|---|
| Claude Code | `claude -p ...` không kèm prompt thì tự đọc stdin |
| Gemini CLI | stdin là prompt; `-p "..."` được **nối thêm** vào sau stdin (bot dùng `-p "Follow the instructions above."`) |
| Codex CLI | `codex exec ... -` (dấu `-` ở cuối) |
| Antigravity | **Phải** dùng `--input-format stream-json` và gửi tin JSON (mục 5.4) |

### 2.3 Quản lý tiến trình (Python asyncio)

```python
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)   # không bật cửa sổ console
proc = await asyncio.create_subprocess_exec(
    *argv, cwd=str(project_path), env=env, creationflags=NO_WINDOW,
    stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
)
```

- **Ghi prompt rồi đóng stdin** (`proc.stdin.write(...)`, `await drain()`, `close()`). Bỏ qua `BrokenPipeError` / `ConnectionResetError` nếu CLI thoát sớm.
- **Đọc stdout theo từng dòng ngay khi có**, không chờ CLI chạy xong. Nhờ vậy hiện được tiến độ trực tiếp (Claude/Antigravity phát sự kiện JSON từng dòng).
- **Chờ `proc.wait()` có giới hạn thời gian** (bot đặt 60 phút: `TASK_TIMEOUT_MIN`). Quá giờ thì **giết cả cây tiến trình**:
  `taskkill /PID <pid> /T /F`. Chỉ `proc.kill()` thì không giết được các tiến trình con.
- **Tiến trình đã thoát thì chỉ chờ luồng đọc tối đa 10 giây.** Agent có thể để lại tiến trình con (ví dụ `npm run dev`) vẫn giữ ống stdout, nên chờ đọc tới hết (EOF) sẽ treo mãi.
- **Giải mã output** bằng `decode("utf-8", "replace")`, rồi đổi `\r\n` thành `\n`, vì Codex trên Windows in ra `\r\n`.

### 2.4 Thư mục làm việc

Đặt `cwd` bằng **thư mục dự án**. Mọi CLI đều lấy `cwd` làm workspace: đọc `CLAUDE.md`/`GEMINI.md`/`AGENTS.md`, và lưu phiên theo thư mục.

### 2.5 Nhiều tài khoản cho cùng một nền tảng

Mỗi tài khoản cần một thư mục cấu hình riêng, đặt qua biến môi trường của tiến trình con:

| CLI | Biến môi trường | Ghi chú |
|---|---|---|
| Claude Code | `CLAUDE_CONFIG_DIR=<thư mục>` | Thông tin đăng nhập và `.claude.json` nằm trong thư mục này |
| Codex | `CODEX_HOME=<thư mục>` | Có `auth.json`, `config.toml` |
| Gemini CLI / Antigravity | `HOME` **và** `USERPROFILE` = `<thư mục>` | Cả hai đều đọc `~/.gemini`. Trên Windows, Node lấy home từ `USERPROFILE` |

Đăng nhập một lần cho thư mục riêng: chạy CLI với cùng biến môi trường ở chế độ tương tác (script `login.py` của bot).

> Bot hiện dùng **đăng nhập mặc định**. Khi đổi tài khoản Claude trong Claude Code (`/login`), bot tự nhận ra (mục 3.7).

### 2.6 Phân loại kết quả

Mọi lượt chạy được xếp vào **một** trong các loại sau:

| Loại | Cách nhận biết | Bot xử lý |
|---|---|---|
| Thành công | Thoát mã 0 và có câu trả lời | Đăng câu trả lời, chuyển việc |
| Hết quota | Xem các mẫu bên dưới; Claude có thêm `rate_limit_event.status = "rejected"` | Đánh dấu model tạm hết, chuyển sang model dự phòng |
| Lỗi đăng nhập | `401 Unauthorized`, `not logged in`, `Please set an Auth method`, `IneligibleTierError`… | Như hết quota (tạm ngưng 24 giờ), chuyển dự phòng |
| Lỗi tạm thời | `503`, `UNAVAILABLE`, `service is currently unavailable`, `overloaded`, `timed out` | Chờ 15 giây rồi thử lại, tối đa 2 lần |
| Mất phiên | Claude: `No conversation found with session ID`; Antigravity: **cảnh báo** `conversation "<id>" not found` | Xoá mã phiên, chạy lại với prompt đầy đủ |
| Lỗi khác | Mã thoát khác 0, hoặc không có câu trả lời | Tạm dừng đầu việc, tag chủ dự án |

Mẫu nhận biết hết quota, lấy từ `providers.py`:

```python
# Gần như chắc chắn là hết hạn mức (tin cả khi CLI thoát mã 0, nếu câu trả lời ngắn < 400 ký tự):
STRONG_QUOTA = re.compile(
    r"usage limit|limit reached|quota exceeded|exceeded (?:your )?(?:current )?quota|resource[_ ]exhausted"
    r"|hit your (?:usage )?limit|out of credits|credit balance is too low|rate limit exceeded"
    r"|too many requests|(?:weekly|daily|session) limit", re.I)
# Chỉ tin khi CLI cũng báo lỗi (để không nhầm khi agent đang code tính năng "rate limit"):
WEAK_QUOTA = re.compile(
    r"\b429\b|rate.?limited|rate.?limit\s*(?:error|reached|exceeded|hit)|quota\s*(?:exceeded|exhausted|reached)"
    r"|capacity.?exhausted|exceeded\b.{0,40}\bquota|try again (?:later|in \d|at \d)", re.I)
```

> ⚠️ Bài học khi test: đừng bắt chữ `quota` hay `rate limit` đứng một mình. Lần đầu bot hiểu nhầm là hết quota, chỉ vì traceback lỗi chứa đoạn code `print("Đã thêm rate limit cho OTP")`.

**Tìm giờ mở lại quota**, theo thứ tự ưu tiên:
1. Claude `rate_limit_event.resetsAt` (giây epoch).
2. Chuỗi `"...|1760000000"`.
3. `"in 2 hours 13 minutes"`.
4. `"resets 3pm"`.
5. Không tìm được thì dùng thời gian chờ mặc định theo nền tảng: Claude 300 phút, Gemini 60, Codex 300, Antigravity 60 (đổi bằng `COOLDOWN_*_MIN` trong `.env`).

### 2.7 Nhớ hội thoại (mở lại phiên)

| CLI | Mở lại phiên | Mã phiên lấy ở đâu |
|---|---|---|
| Claude Code | `--resume <session_id>` | `session_id` trong JSON kết quả |
| Antigravity | `--conversation <conversation_id>` | `result.conversation_id` |
| Codex | `codex exec resume <SESSION_ID>` hoặc `--last` | (bot chưa dùng) |
| Gemini CLI | (bot không dùng) | `session_id` trong JSON |

Quy tắc bot rút ra:
- Mã phiên **gắn với nền tảng và tài khoản**. Đổi nền tảng thì phải xoá mã phiên cũ (`db.clear_cli_sessions`).
- Khi mở lại phiên, chỉ gửi **tin mới + các tin của người khác kể từ lượt trước của mình + tình trạng git**, không gửi lại toàn bộ luật. Nếu mở lại thất bại thì chạy lại với **prompt đầy đủ**.

### 2.8 Chế độ quyền

| Chế độ của bot | Claude Code | Antigravity | Gemini CLI | Codex |
|---|---|---|---|---|
| 🛡 **An toàn** (chỉ đọc/sửa file) | `--permission-mode acceptEdits` | `--mode accept-edits` | `--approval-mode auto_edit` | `--sandbox workspace-write` |
| ⚡ **Toàn quyền** (chạy mọi lệnh) | `--permission-mode bypassPermissions` | `--dangerously-skip-permissions` | `--approval-mode yolo` | `--sandbox danger-full-access` |

> Ở chế độ An toàn, mỗi CLI xử lý lệnh bị chặn **khác nhau**. Claude bỏ qua lệnh đó và làm tiếp. Antigravity **kết thúc luôn lượt, không trả lời** (mục 5.7). Vì vậy bot thêm vào prompt: *"Chế độ An toàn: KHÔNG chạy lệnh terminal… Cần chạy lệnh thì ghi rõ trong câu trả lời."*

### 2.9 File ngữ cảnh trong dự án

| File | Ai đọc | Nội dung bot tạo |
|---|---|---|
| `AGENTS.md` | Codex đọc trực tiếp; các CLI khác đọc qua import | Luật chung, phân vai, quy trình bàn giao |
| `CLAUDE.md` | Claude Code | `@AGENTS.md` (import) |
| `GEMINI.md` | Gemini CLI | `@AGENTS.md` (import) |
| `.ai/STATE.md`, `.ai/DECISIONS.md`, `.ai/rules/<vai>.md` | Bot đưa thẳng vào prompt | Trạng thái, quyết định, luật riêng từng vai |

---

## 3. Claude Code CLI 2.1.216

### 3.1 Cài đặt và đăng nhập

```bash
npm i -g @anthropic-ai/claude-code
claude            # lần đầu: đăng nhập (/login), xong gõ /exit
claude --version  # 2.1.216 (Claude Code)
```

- Trên Windows, `claude.cmd` gọi file chạy thật `node_modules\@anthropic-ai\claude-code\bin\claude.exe`.
- **Thông tin đăng nhập:** `~/.claude/.credentials.json`. Có `CLAUDE_CONFIG_DIR` thì nằm trong thư mục đó.
- **Thông tin tài khoản:** `~/.claude.json` → khoá `oauthAccount`, gồm `accountUuid`, `emailAddress`, `organizationName`, `organizationUuid`, `billingType`, `userRateLimitTier`, `subscriptionCreatedAt`… Bot dùng `accountUuid` để nhận ra khi đổi tài khoản.

### 3.2 Lệnh bot dùng (chính xác)

```text
claude -p --output-format stream-json --verbose --permission-mode acceptEdits --model sonnet [--resume <session_id>]
         └── prompt gửi qua stdin
```

- Không cần tiến độ trực tiếp thì dùng `--output-format json` (bỏ `--verbose`).
- **Ở chế độ `-p`, `--output-format stream-json` bắt buộc có `--verbose`.**
- Toàn quyền: `--permission-mode bypassPermissions`.

**Các cờ đáng chú ý** (trích `claude --help`, bản 2.1.216):

| Cờ | Ý nghĩa |
|---|---|
| `-p, --print` | Chạy một lượt rồi in kết quả (chạy ngầm). Trong chế độ này hộp thoại "tin tưởng thư mục" được bỏ qua |
| `--output-format text\|json\|stream-json` | Định dạng kết quả (chỉ dùng với `-p`) |
| `--input-format text\|stream-json` | Định dạng đầu vào (chỉ dùng với `-p`) |
| `--model <alias\|tên>` | Tên ngắn `opus`, `sonnet`, `haiku`, `fable`, hoặc tên đầy đủ (`claude-opus-5-5`, `claude-sonnet-5-5`, `claude-haiku-5-5`, `claude-fable-5`…) |
| `--permission-mode` | `acceptEdits`, `auto`, `bypassPermissions`, `manual`, `dontAsk`, `plan` |
| `--dangerously-skip-permissions` | Bỏ mọi kiểm tra quyền (tương đương `bypassPermissions`) |
| `-r, --resume [id]` | Mở lại phiên theo ID |
| `-c, --continue` | Mở lại phiên gần nhất trong thư mục |
| `--session-id <uuid>` | Tự đặt ID cho phiên mới |
| `--fork-session` | Khi mở lại, tạo ID mới thay vì dùng tiếp ID cũ |
| `--no-session-persistence` | Không lưu phiên (chỉ với `-p`) |
| `--fallback-model <model,...>` | Model thay thế khi model chính quá tải (chỉ với `-p`) |
| `--effort low\|medium\|high\|xhigh\|max` | Mức suy luận |
| `--allowedTools` / `--disallowedTools` | Danh sách công cụ cho phép / cấm, ví dụ `"Bash(git *) Edit"` |
| `--append-system-prompt` | Thêm vào system prompt |
| `--add-dir` | Cho phép truy cập thêm thư mục |
| `--mcp-config` | Nạp MCP server |
| `--bare` | Chế độ tối giản: không hook, không tự đọc CLAUDE.md, chỉ xác thực bằng API key |

### 3.3 Kết quả `--output-format json`

Một đối tượng JSON duy nhất. Các khoá quan sát được khi chạy thật:

```json
{
  "type": "result", "subtype": "success", "is_error": false,
  "result": "<câu trả lời cuối>",
  "session_id": "5ff2b70d-2881-4b3a-ab27-a19ead4cfa33",
  "total_cost_usd": 0.0444, "duration_ms": 7556, "num_turns": 3,
  "permission_denials": [], "usage": {...}, "modelUsage": {...},
  "stop_reason": "...", "api_error_status": null, "uuid": "..."
}
```

- Thành công: `is_error == false` và `subtype == "success"`.
- Tin nhắn chứa `result` có thể là thông báo lỗi, ví dụ hết quota (`"Claude AI usage limit reached|<epoch>"`).

### 3.4 Sự kiện `--output-format stream-json --verbose`

Mỗi dòng là một JSON. Lượt "đọc file, sửa file, trả lời" bằng Haiku cho ra **130 dòng**, chạy 7,5 giây:

| Dòng (`type` / `subtype`) | Nội dung | Bot dùng để |
|---|---|---|
| `system` / `init` | `session_id`, `model`, `tools[]`, `permissionMode`, `claude_code_version`, `cwd`, `mcp_servers`, `skills`… | (bỏ qua) |
| `rate_limit_event` | `rate_limit_info: {status, resetsAt, rateLimitType, overageStatus, overageDisabledReason, isUsingOverage}` | **Quota thật** |
| `system` / `thinking_tokens` | Rất nhiều dòng | Bỏ qua |
| `assistant` | `message.content[]`: `{type:"thinking"}`, `{type:"tool_use", name, input}`, `{type:"text", text}` | Dòng tiến độ, "💭 đang nói" |
| `user` | `message.content[]`: `{type:"tool_result", is_error, content}` | Phát hiện bị chặn quyền |
| `result` | Giống JSON ở mục 3.3 | Câu trả lời cuối, `session_id` |

Ví dụ thật (rút gọn):

```json
{"type":"rate_limit_event","rate_limit_info":{"status":"allowed","resetsAt":1791884400,"rateLimitType":"five_hour","overageStatus":"rejected","overageDisabledReason":"org_level_disabled","isUsingOverage":false}}
{"type":"assistant","message":{"content":[{"type":"tool_use","name":"Read","input":{"file_path":"C:\\...\\note.txt"}}]}}
{"type":"assistant","message":{"content":[{"type":"tool_use","name":"Edit","input":{"replace_all":false,"file_path":"C:\\...\\note.txt", "...": "..."}}]}}
{"type":"assistant","message":{"content":[{"type":"text","text":"DONE"}]}}
{"type":"result","subtype":"success","is_error":false,"result":"DONE","session_id":"...","total_cost_usd":0.0444,"duration_ms":7556,...}
```

**Tên công cụ → dòng tiến độ** (khoá `input` dùng làm đích):

| Công cụ Claude | Hiển thị | Khoá `input` |
|---|---|---|
| `Read` | 📖 Đọc | `file_path` |
| `Edit`, `MultiEdit`, `NotebookEdit` | ✏️ Sửa | `file_path` / `notebook_path` |
| `Write` | 📝 Tạo/ghi | `file_path` |
| `Bash`, `PowerShell` | ▶️ Chạy | `command` |
| `Glob`, `Grep` | 🔎 Tìm | `pattern` |
| `WebFetch`, `WebSearch` | 🌐 | `url` / `query` |
| `TodoWrite` | 🗒 Lập kế hoạch | — |
| `Task`, `Agent` | 🤝 Giao việc phụ | `description` |

### 3.5 Quota thật của Claude

`rate_limit_event.rate_limit_info`:

- `status`: `allowed` (bình thường), `allowed_warning` (sắp hết), `rejected` (hết).
- `resetsAt`: giây epoch lúc quota được làm mới.
- `rateLimitType`: `five_hour`, `seven_day`, `seven_day_opus`…

Bot lưu thông tin này vào `agents.quota_hint` để hiện *"Quota 5 giờ: bình thường, làm mới 16:28"*. Khi `status == "rejected"`, bot coi là hết quota và lấy `resetsAt` làm giờ mở lại chính xác.

> Quota tính theo **tài khoản**, nên mọi nhân viên dùng chung một tài khoản Claude thì chung quota.

### 3.6 Mở lại phiên

- `--resume <session_id>` dùng tiếp đúng phiên đó. ID không đổi, trừ khi có `--fork-session`.
- ID không tồn tại thì Claude báo **lỗi**: `No conversation found with session ID: <id>`.
- Phiên được lưu theo thư mục làm việc và thư mục cấu hình. Đổi tài khoản hoặc `CLAUDE_CONFIG_DIR` thì có thể không mở lại được.

### 3.7 Nhận ra khi đổi tài khoản

Khi hết quota, bot ghi `oauthAccount.accountUuid` của lúc đó vào `agents.quota_account`. Cứ 30 giây bot đọc lại `~/.claude.json` (hoặc `<CLAUDE_CONFIG_DIR>/.claude.json`). Nếu `accountUuid` khác, bot cho dùng lại ngay và báo vào group.

> Đã gặp thật: 16:36 tài khoản 1 hết quota, đúng 16:36:51 chủ dự án đăng nhập tài khoản 2 (file `.credentials.json` đổi thời gian sửa), nhưng bản cũ của bot vẫn chờ tới 19:50.

### 3.8 Những điểm dễ vấp

- `acceptEdits` chặn `Bash`, nhưng Claude **vẫn làm tiếp lượt**. Các lệnh bị chặn nằm trong `result.permission_denials`.
- `--output-format stream-json` mà thiếu `--verbose` (ở chế độ `-p`) thì lỗi.
- Kết quả có thể chứa link markdown dạng `[file](file:///C:/...)`. Bot đổi thành tên file trước khi gửi Telegram.

---

## 4. Gemini CLI 0.63.0

### 4.1 Cài đặt

```bash
npm i -g @google/gemini-cli
gemini --version   # 0.63.0
gemini             # chọn "Sign in with Google" (đăng nhập trên trình duyệt), xong gõ /quit
```

Sau khi đăng nhập:
- `~/.gemini/settings.json` có `{"security": {"auth": {"selectedType": "oauth-personal"}}}`.
- Token lưu ở `~/.gemini/oauth_creds.json`.

### 4.2 Các cờ chạy ngầm (theo tài liệu chính thức `docs/cli/cli-reference.md`)

| Cờ | Ý nghĩa |
|---|---|
| `-p, --prompt "<text>"` | Ép chạy không tương tác. Nội dung `-p` được **nối vào sau stdin** |
| `-m, --model` | `auto` (mặc định), `pro`, `flash`, `flash-lite`, hoặc tên model cụ thể |
| `--approval-mode` | `default`, `auto_edit`, `yolo`, `plan` (cờ `--yolo` cũ đã bỏ) |
| `-o, --output-format` | `text`, `json`, `stream-json` |

### 4.3 Lệnh bot dùng

```text
gemini --output-format json --approval-mode auto_edit -p "Follow the instructions above." [--model flash]
       └── prompt chính gửi qua stdin
```

### 4.4 Kết quả JSON

```json
{"session_id": "...", "response": "<câu trả lời>", "stats": {...}, "error": {"type": "...", "message": "...", "code": 41}}
```

Có khoá `error` nghĩa là lỗi.

### 4.5 ❌ Tình trạng: tài khoản Google cá nhân bị chặn (13/10/2026)

**Chưa đăng nhập:**
```json
{"session_id":"b0e2ee46-...","error":{"type":"Error","message":"Please set an Auth method in your C:\\Users\\Admin\\.gemini\\settings.json or specify one of the following environment variables before running: GEMINI_API_KEY, GOOGLE_GENAI_USE_VERTEXAI, GOOGLE_GENAI_USE_GCA","code":41}}
```

**Đã đăng nhập bằng tài khoản cá nhân** (gói `free-tier`, "Gemini Code Assist for individuals"):
```text
Error authenticating: IneligibleTierError: This client is no longer supported for Gemini Code Assist for individuals.
To continue using Gemini, please migrate to the Antigravity suite of products: https://antigravity.google
  ineligibleTiers: [ { reasonCode: 'UNSUPPORTED_CLIENT', tierId: 'free-tier', tierName: 'Gemini Code Assist for individuals' } ]
```

**Cách khác:**
- `GEMINI_API_KEY` (khoá API từ AI Studio) là cách chính thức để chạy ngầm. **Chưa thử.**
- **Dùng Antigravity CLI** (mục 5). Bot đang dùng cách này.

### 4.6 File ngữ cảnh

`GEMINI.md` trong dự án chứa `@AGENTS.md` (Gemini CLI hỗ trợ import `@file`). Đổi tên file ngữ cảnh trong `.gemini/settings.json` → `context.fileName`.

---

## 5. Antigravity CLI (`agy`) 1.3.x

**Đây là cách dùng Gemini đang chạy được với tài khoản Google cá nhân.** CLI chính thức của Google, mô tả trên winget: *"giao diện dòng lệnh (TUI) của Antigravity, cùng khả năng agent như Antigravity 2.0: suy luận nhiều bước, sửa nhiều file, gọi công cụ, lịch sử hội thoại"*.

### 5.1 Cài đặt

```bash
winget install --id Google.AntigravityCLI -e --source winget --accept-source-agreements --accept-package-agreements --silent
```

- **Phải ghi `--source winget`.** Nếu không, winget đi tìm cả ở `msstore` và lỗi `WinHttpSendRequest: 12002 ... 0x80072ee2` (hết thời gian chờ).
- Bộ cài tải từ `https://storage.googleapis.com/antigravity-public/antigravity-cli/1.3.3-5524738307653632/windows-x64/cli_windows_x64.exe` (đã kiểm hash).
- Lệnh tắt: `agy`, nằm ở `%LOCALAPPDATA%\Microsoft\WinGet\Links\agy.exe`. Đây là symlink tới `...\WinGet\Packages\Google.AntigravityCLI_Microsoft.Winget.Source_8wekyb3d8bbwe\agy.exe`.
- **`agy.exe` là file chạy thật, không phải `.cmd`**, nên không đi qua `cmd.exe`.
- CLI **tự cập nhật**: cài bản 1.3.3, vài giờ sau `agy --version` báo 1.3.4. Cập nhật tay bằng `agy update`.

### 5.2 Đăng nhập và model

- **Dùng chung đăng nhập với Antigravity IDE** (thư mục `~/.gemini`). Máy đã đăng nhập IDE thì CLI dùng được ngay, **kể cả khi IDE đang tắt**.
- `agy models` liệt kê model (13/10/2026):

```text
gemini-3.8-flash-high      Gemini 3.8 Flash (High)
gemini-3.8-flash-medium    Gemini 3.8 Flash (Medium)
gemini-3.8-flash-low       Gemini 3.8 Flash (Low)
claude-sonnet-4-6          Claude Sonnet 4.6 (Thinking)
claude-opus-4-6-thinking   Claude Opus 4.6 (Thinking)
gpt-oss-120b-medium        GPT-OSS 120B (Medium)
```

> Các model Claude và GPT-OSS ở đây chạy bằng **quota Antigravity**, tách biệt với gói Claude của bạn, nên dùng làm model dự phòng rất hợp.

### 5.3 Toàn bộ cờ (`agy --help`, bản 1.3.3)

```text
  --add-dir                       Add a directory to the workspace (repeatable)
  --agent                         Agent for the current CLI session
  -c / --continue                 Continue the most recent conversation
  --conversation                  Resume a previous conversation by ID
  --dangerously-skip-permissions  Auto-approve all tool permission requests without prompting
  --disable-slash-commands        Disable slash command and skill expansion in print mode
  --effort                        Reasoning effort (low|medium|high|xhigh|max)
  -i / --prompt-interactive       Run an initial prompt interactively and continue the session
  --input-format                  Input format for print mode (text, stream-json). stream-json reads one NDJSON
                                  message per line from stdin and runs a turn for each; it requires --output-format stream-json
  --json-schema                   JSON schema to enforce structured output
  --log-file                      Override CLI log file path
  --mode                          Agent execution mode (accept-edits, plan)
  --model                         Model for the current CLI session
  --new-project                   Create a new project for this session
  --output-format                 Output format for print mode (text, json, stream-json)
  -p / --print / --prompt         Run a single prompt non-interactively and print the response
  --print-timeout                 Optional time limit for print mode; 0 waits until the turn completes
  --project                       Project ID or project name for the current CLI session
  --remote-control                Create a remote connection for the CLI session on start up
  --sandbox                       Run in a sandbox with terminal restrictions enabled
Subcommands: agent(s), changelog, help, install, mcp, mic-serve, models, plugin(s), remote-control, update
```

### 5.4 ⚠️ Chạy ngầm: cách đúng và các cách sai đã thử

| Đã thử | Kết quả |
|---|---|
| `agy -p --output-format json --model X` (stdin) | ❌ `Error: -p took "--output-format" as its prompt, so the intended prompt was left as an argument and ignored.` (`-p` **nhận giá trị** ngay sau nó) |
| `agy --output-format json --model X -p` (để `-p` cuối) | ❌ In ra hướng dẫn sử dụng, mã thoát 2 |
| `agy --output-format json --model X -p -` | ⚠️ Chạy được (mã 0) nhưng prompt **chính là dấu "-"**, stdin bị bỏ qua. Model trả lời "How can I help you today?" |
| `agy --input-format stream-json --output-format stream-json ... -p -` | ❌ `Error: --input-format stream-json reads prompts from stdin, so a prompt given on the command line would be ignored` |
| stdin `{"type":"user","message":{...}}` (thiếu `event`) | ❌ `result.status = "ERROR"`, `error: stream input message is missing the "event" field` |
| ✅ **Cách đúng** (bên dưới) | Thành công |

**Cách đúng (bot đang dùng):**

```text
agy --input-format stream-json --output-format stream-json --mode accept-edits --model gemini-3.8-flash-high [--conversation <id>]
```

Stdin là **một dòng JSON**, kết thúc bằng `\n`, sau đó đóng stdin:

```json
{"event":"user","message":{"role":"user","content":[{"type":"text","text":"<prompt>"}]}}
```

- Mỗi dòng stdin là **một lượt**. Đóng stdin thì CLI chạy xong các lượt rồi thoát (`stream input closed after N turn(s)`).
- `content` chỉ nhận khối `{"type":"text"}` (thông báo lỗi của CLI: `stream input content block type %q is not supported (only %q)`).
- Python: `json.dumps(msg, ensure_ascii=False) + "\n"`, rồi `encode("utf-8")`.

### 5.5 Sự kiện stdout (`--output-format stream-json`)

Mỗi dòng có khoá `event`:

```json
{"event":"init","conversation_id":"a89bc3e3-...","init":{"model":"gemini-3.8-flash-low","cwd":"C:\\...","tools":[...60 công cụ...],"permission_mode":"request-review"}}
{"event":"step_update","step_update":{"conversation_id":"de978043-...","step_index":0,"state":"DONE","step_type":"user_input"}}
{"event":"step_update","step_update":{"conversation_id":"...","step_index":1,"state":"DONE","step_type":"agent_response","duration_seconds":4.19,"usage":{"input_tokens":11782,"output_tokens":100,"thinking_tokens":0,"cache_read_tokens":0,"total_tokens":11882}}}
{"event":"step_update","step_update":{"conversation_id":"...","step_index":2,"state":"ACTIVE","step_type":"tool","tool_name":"view_file","tool_info":{"name":"view_file","parameters":{"AbsolutePath":"C:\\...\\note.txt"}}}}
{"event":"step_update","step_update":{"conversation_id":"...","step_index":2,"state":"DONE","step_type":"tool","tool_name":"view_file","duration_seconds":0.052,"tool_info":{...}}}
{"event":"step_update","step_update":{"...":"...","state":"ACTIVE","step_type":"tool","tool_name":"replace_file_content","tool_info":{"parameters":{"TargetFile":"C:\\...\\note.txt"}}}}
{"event":"step_update","step_update":{"...":"...","state":"ACTIVE","step_type":"tool","tool_name":"run_command","tool_info":{"parameters":{"CommandLine":"echo hi"}}}}
{"event":"result","result":{"conversation_id":"de978043-...","status":"SUCCESS","response":"...","duration_seconds":11.16,"num_turns":1,"usage":{...},"denied_actions":[{"action":"command","display_name":"RunCommand"}]}}
```

- `state`: `ACTIVE` (bắt đầu), `DONE` (xong), `ERROR`. Bot hiện dòng tiến độ khi gặp `tool` + `ACTIVE`.
- `step_type`: `user_input`, `agent_response`, `tool`. Có lúc kèm `text_delta`.
- `result.status`: `SUCCESS` / `ERROR`. Câu trả lời ở `result.response`, lỗi ở `result.error`.
- `--mode accept-edits` mà `init.permission_mode` vẫn ghi `"request-review"`: đã quan sát thật, sửa file vẫn được, lệnh vẫn bị chặn.

### 5.6 Công cụ của agent (60 công cụ, lấy từ `init.tools`)

```text
ask_custom_permission, ask_permission, ask_question, browser_click_element, browser_drag_pixel_to_pixel, browser_get_dom,
browser_get_network_request, browser_input, browser_list_network_requests, browser_mouse_down, browser_mouse_up,
browser_move_mouse, browser_press_key, browser_refresh_page, browser_resize_window, browser_scroll, browser_scroll_dom,
browser_select_option, browser_subagent, call_mcp_tool, capture_browser_console_logs, capture_browser_screenshot,
click_browser_pixel, command_status, define_subagent, delete_knowledge, execute_browser_javascript, find_by_name, finish,
generate_image, grep_search, invoke_subagent, list_browser_pages, list_dir, list_permissions, list_plugin_accounts,
list_resources, manage_inbox, manage_subagents, manage_task, multi_replace_file_content, notebook_edit, notebook_execution,
open_browser_url, read_browser_page, read_resource, read_url_content, replace_file_content, run_command, run_workflow,
schedule, search_marketplace, search_web, sed_file, send_command_input, send_message, view_file, wait, wait_5_seconds,
write_to_file
```

Bảng tham số bot dùng để hiện tiến độ:

| Công cụ | Hiển thị | Tham số |
|---|---|---|
| `view_file`, `view_file_outline` | 📖 Đọc | `AbsolutePath` |
| `view_code_item` | 📖 Đọc | `File` |
| `replace_file_content`, `multi_replace_file_content` | ✏️ Sửa | `TargetFile` |
| `write_to_file` | 📝 Tạo/ghi | `TargetFile` |
| `run_command` | ▶️ Chạy | `CommandLine` |
| `find_by_name` | 🔎 Tìm file | `Pattern` |
| `grep_search`, `codebase_search` | 🔎 Tìm | `Query` |
| `list_dir` | 🔎 Xem thư mục | `DirectoryPath` |
| `search_web` / `read_url_content` | 🌐 | `query` / `Url` |
| `browser_*` | 🌐 | — |

### 5.7 ⚠️ Quyền ở chế độ chạy ngầm

- `--mode accept-edits`: **sửa file được**. Nhưng **lệnh terminal bị tự động từ chối, và cả lượt kết thúc mà không có câu trả lời**. Stderr ghi:

  ```text
  jetski: no output produced — a tool required the "command" permission that headless mode cannot prompt for,
  so it was auto-denied. Add an allow-rule under permissions.allow in settings.json (e.g. command(<target>)).
  Alternatively, re-run with --dangerously-skip-permissions to auto-approve all tools.
  ```

  `result.status` vẫn là `"SUCCESS"`, `response` rỗng, kèm `denied_actions: [{"action":"command","display_name":"RunCommand"}]`. Bot coi trường hợp này là **lỗi** và giải thích *"thao tác bị chặn ở chế độ An toàn"*.
- `--dangerously-skip-permissions`: tự duyệt mọi công cụ (chế độ ⚡ Toàn quyền).
- Có thể **cho phép từng lệnh** qua `permissions.allow` trong `settings.json`, ví dụ `command(npm run build)`. **Chưa thử.**
- `--sandbox`: chạy lệnh trong sandbox có giới hạn. **Chưa thử.**

### 5.8 ⚠️ Mở lại phiên

- `--conversation <id>` dùng tiếp cuộc trò chuyện. Kết quả vẫn trả `conversation_id` cũ.
- **ID không tồn tại thì không báo lỗi.** Stderr chỉ cảnh báo `warning: conversation "<id>" not found` rồi **lặng lẽ mở cuộc trò chuyện mới** (`status: SUCCESS`, `conversation_id` mới). Nếu không bắt cảnh báo này, agent nhận prompt rút gọn của lượt mở lại mà không có bối cảnh. Bot bắt regex `conversation "[^"]*" not found` trên stderr, xoá mã phiên và chạy lại với prompt đầy đủ.

### 5.9 Lỗi tạm thời phía máy chủ

Gặp vài lần trong ngày 13/10/2026:

```text
error: Eligibility check failed: UNAVAILABLE (code 503): The service is currently unavailable.
```

Lỗi này là **tạm thời**. Bot chờ 15 giây rồi thử lại, tối đa 2 lần.

### 5.10 Thời gian và lượng token đo thật

| Lượt | Model | Thời gian | Token |
|---|---|---|---|
| "Reply OK" | gemini-3.8-flash-low | 2,6 giây | ~11,8 nghìn token vào (system prompt riêng của agy đã ~11,7 nghìn), 33 ra |
| Đọc file, sửa file, thử chạy lệnh | gemini-3.8-flash-low | 11,2 giây | 36,5 nghìn vào, 377 ra |
| Tạo `hello.txt` + cập nhật `STATE.md` (qua bot) | gemini-3.8-flash-low | 1 phút 15 giây | — |
| Kiểm tra quota (probe) | gemini-3.8-flash-high | vài giây | — |

### 5.11 Quota và tài khoản

- Output **không có thông tin quota** như Claude (chỉ có `usage` token). Bot chỉ biết hết quota khi gặp lỗi.
- Bot **chưa đọc được** CLI đang đăng nhập tài khoản Google nào, nên không tự nhận ra khi đổi tài khoản. Khi đổi, dùng `/quota reset <bot>` hoặc nút 🔋.

### 5.12 Công cụ trình duyệt

Đã thử nhờ agent mở `https://example.com`, chụp màn hình và lưu file. **Gemini không dùng `capture_browser_screenshot`** mà định chạy `python --version; node --version` để viết script. Lệnh bị chặn ở An toàn, và lượt kết thúc không có kết quả. Vì vậy **chức năng chụp màn hình do bot tự làm bằng Playwright** (`aiteam/preview.py`), không giao cho agent.

---

## 6. Antigravity IDE nội bộ: `agentapi`

> ⚠️ **Ghi chép nghiên cứu. KHÔNG dùng trong bot.** Đây là cổng nội bộ của Antigravity, không có tài liệu chính thức, dễ hỏng khi cập nhật. Nên dùng Antigravity CLI (mục 5).

- File `~/.gemini/antigravity/bin/agentapi.bat` (do app Antigravity bản đứng riêng tạo) gọi:
  `"...\Programs\Antigravity\resources\bin\language_server.exe" agentapi %*`
- Trong Antigravity IDE 1.107.0, chương trình tương ứng là
  `...\Antigravity IDE\resources\app\extensions\antigravity\bin\language_server_windows_x64.exe`.
- Lệnh có sẵn: `agentapi new-conversation [--model=flash_lite|flash|pro] [--title=] [--profile=] <prompt>`, `send-message <recipient_id> <content>`, `get-conversation-metadata <conversation_id>`.
- Biến môi trường cần có (tìm thấy trong file chạy): `ANTIGRAVITY_LS_ADDRESS`, `ANTIGRAVITY_CSRF_TOKEN`, `ANTIGRAVITY_PROJECT_ID`, `ANTIGRAVITY_CONVERSATION_ID`… IDE tự đặt các biến này cho tiến trình con của agent.
- Đã nối được từ bên ngoài:
  - `ANTIGRAVITY_LS_ADDRESS=127.0.0.1:<cổng gRPC>`. Cổng tìm bằng `Get-NetTCPConnection -OwningProcess <pid language_server> -State Listen`; trong 3 cổng, cổng đúng là cổng trả lỗi nghiệp vụ chứ không phải lỗi kết nối.
  - `ANTIGRAVITY_CSRF_TOKEN` lấy từ tham số `--csrf_token` trên dòng lệnh của tiến trình.
- Bị chặn ở bước tạo cuộc trò chuyện:
  - Thiếu project: `project_id is required when providing project_env_config`.
  - Có project: `projectsStore is nil, but projectEnvConfig was provided`.

  Kho project chỉ bật khi language server chạy `--subclient_type=hub` (Agent Manager). IDE chạy `--subclient_type ide`.
- Project lưu ở `~/.gemini/config/projects/<uuid>.json` (`id`, `name`, `projectResources.resources[].gitFolder.folderUri`, `settings.artifactReviewMode`…). Trò chuyện ngoài project dùng `project_id = "outside-of-project"`.

---

## 7. Codex CLI 0.162.1

### 7.1 Cài đặt và đăng nhập

```bash
npm i -g @openai/codex
codex --version        # codex-cli 0.162.1
codex login            # đăng nhập tài khoản ChatGPT (cần gói có Codex)
codex login status     # "Not logged in" khi chưa đăng nhập
```

- Thư mục cấu hình mặc định `~/.codex`, đổi bằng `CODEX_HOME`. Có `auth.json` và `config.toml`.
- Chạy một lần bằng API key: `CODEX_API_KEY=<key> codex exec ...`.

### 7.2 Lệnh bot dùng

```text
codex exec --skip-git-repo-check --sandbox workspace-write [--model <model>] -
           └── prompt qua stdin (dấu "-")
```

- Toàn quyền: `--sandbox danger-full-access`.
- `--full-auto` **đã bị bỏ** (in cảnh báo). Thay bằng `--sandbox workspace-write`.
- Sandbox mặc định của `codex exec` là **read-only**.

### 7.3 Đầu ra

- **stdout: chỉ có câu trả lời cuối.** stderr là tiến độ (log).
- `--json` biến stdout thành JSON Lines các sự kiện (`thread.started`, `turn.completed`, `error`…). Bot **chưa dùng**, nên chưa có tiến độ từng thao tác cho Codex.
- `-o, --output-last-message <file>` ghi câu trả lời cuối ra file.
- `--output-schema <file>` ép câu trả lời theo JSON Schema.
- Mở lại phiên: `codex exec resume <SESSION_ID>` hoặc `codex exec resume --last`. Bot chưa dùng.
- Các cờ khác: `--ephemeral` (không lưu phiên), `--ignore-user-config`, `--ignore-rules`.

### 7.4 Chưa đăng nhập (đã chạy thật qua bot)

```text
warning: Falling back from WebSockets to HTTPS transport. unexpected status 401 Unauthorized: Missing bearer or basic authentication in header, url: wss://api.openai.com/v1/responses
ERROR: Reconnecting... 1/5 ... 5/5
ERROR: unexpected status 401 Unauthorized: Missing bearer or basic authentication in header, url: https://api.openai.com/v1/responses
```

Mã thoát 1. Bot xếp đúng vào loại **lỗi đăng nhập**, không phải hết quota.

---

## 8. Bot áp dụng thế nào (bản đồ code)

| File | Hàm / phần | Vai trò |
|---|---|---|
| `aiteam/providers.py` | `PROVIDERS` | Tên, lệnh chạy, model gợi ý, lệnh cài của từng nền tảng |
| | `build_command(agent, autonomy, resume_id, stream)` | Dựng `argv` và biến môi trường (mục 2–7) |
| | `stdin_payload(provider, prompt)` | Prompt thô, hoặc tin JSON của Antigravity |
| | `parse_output(provider, rc, stdout, stderr)` → `Parsed` | `text`, `is_error`, `quota`, `reset_at`, `session_id`, `auth`, `rate`, `session_missing` |
| | `describe_event(line, root)` | Một dòng stream-json → dòng tiến độ (Claude và Antigravity) |
| | `engines(agent)`, `ready_engine(agent)` | Model chính / dự phòng, cái nào còn dùng được |
| | `account_info(engine)` | Tài khoản đang đăng nhập (Claude `.claude.json`, Codex `auth.json`) |
| | `is_transient`, `is_missing_session`, `STRONG_QUOTA`, `WEAK_QUOTA`, `AUTH_ERROR`, `parse_reset` | Phân loại kết quả |
| `aiteam/runner.py` | `Runner._exec` | Chạy tiến trình, đọc từng dòng, giới hạn thời gian, giết cả cây tiến trình |
| | `Runner._run` | Vòng thử: mở lại phiên → mất phiên thì chạy lại → lỗi tạm thời thì thử lại |
| | `Runner._finish` | Cập nhật trạng thái model, chuyển dự phòng, commit git |
| | `Runner.check_account_switch` | Mỗi 30 giây: phát hiện đổi tài khoản |
| | `Runner.probe` | Kiểm tra quota bằng câu "Reply with exactly one word: OK" |
| `aiteam/handoff.py` | `build_group_prompt`, `build_resume_prompt`, `build_prompt` | Ghép prompt (luật, đội, `STATE.md`, git, lịch sử) |
| `aiteam/group.py` | `Progress` | Tin tiến độ tự cập nhật trên Telegram |
| `aiteam/preview.py` | `Preview.capture` | Bật server dev, chụp ☀️/🌙 bằng Edge (Playwright) |

Đoạn trích `build_command` hiện tại:

```python
def build_command(agent, autonomy, resume_id=None, stream=False):
    provider, model = agent["provider"], agent["model"]
    full = autonomy == "full"
    args = [executable(provider)]                      # shutil.which("claude"/"gemini"/"agy"/"codex")
    if provider == "claude":
        output = ["--output-format", "stream-json", "--verbose"] if stream else ["--output-format", "json"]
        args += ["-p", *output, "--permission-mode", "bypassPermissions" if full else "acceptEdits"]
        if model: args += ["--model", model]
        if resume_id: args += ["--resume", resume_id]
    elif provider == "gemini":
        args += ["--output-format", "json", "--approval-mode", "yolo" if full else "auto_edit",
                 "-p", "Follow the instructions above."]
        if model: args += ["--model", model]
    elif provider == "antigravity":
        args += ["--input-format", "stream-json", "--output-format", "stream-json"]
        args += ["--dangerously-skip-permissions"] if full else ["--mode", "accept-edits"]
        if model: args += ["--model", model]
        if resume_id: args += ["--conversation", resume_id]
    elif provider == "codex":
        args += ["exec", "--skip-git-repo-check", "--sandbox", "danger-full-access" if full else "workspace-write"]
        if model: args += ["--model", model]
        args += ["-"]
    return args, agent_env(agent)                      # CLAUDE_CONFIG_DIR / CODEX_HOME / HOME+USERPROFILE
```

---

## 9. Ví dụ tối giản để tự làm lại

Script độc lập, chỉ cần Python 3.10 trở lên. Nó chạy một lượt Claude hoặc Antigravity, in tiến độ trực tiếp và câu trả lời cuối:

```python
"""run_cli.py: chạy một lượt AI CLI ở chế độ ngầm, in tiến độ trực tiếp.
Dùng:  python run_cli.py claude  "C:\\duong\\dan\\du-an"  "Đọc README.md và tóm tắt 3 dòng"
       python run_cli.py agy     "C:\\duong\\dan\\du-an"  "Đọc README.md và tóm tắt 3 dòng"
"""
import asyncio, json, os, shutil, subprocess, sys

NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def command(cli: str) -> tuple[list[str], str]:
    """(argv, cách gửi prompt)"""
    if cli == "claude":
        return [shutil.which("claude"), "-p", "--output-format", "stream-json", "--verbose",
                "--permission-mode", "acceptEdits", "--model", "sonnet"], "raw"
    if cli == "agy":
        return [shutil.which("agy"), "--input-format", "stream-json", "--output-format", "stream-json",
                "--mode", "accept-edits", "--model", "gemini-3.8-flash-medium"], "agy-json"
    raise SystemExit("cli phải là claude hoặc agy")


def payload(kind: str, prompt: str) -> bytes:
    if kind == "agy-json":
        msg = {"event": "user", "message": {"role": "user", "content": [{"type": "text", "text": prompt}]}}
        return (json.dumps(msg, ensure_ascii=False) + "\n").encode("utf-8")
    return prompt.encode("utf-8")


def on_line(line: str) -> None:
    """In tiến độ từ từng dòng sự kiện."""
    try:
        ev = json.loads(line)
    except ValueError:
        return
    if ev.get("type") == "assistant":                                   # Claude
        for c in ev["message"].get("content", []):
            if c.get("type") == "tool_use":
                print("  🔧", c["name"], json.dumps(c.get("input"), ensure_ascii=False)[:100])
    elif ev.get("type") == "rate_limit_event":                          # Claude: quota thật
        print("  📊 quota:", ev["rate_limit_info"])
    elif ev.get("event") == "step_update":                              # Antigravity
        st = ev["step_update"]
        if st.get("step_type") == "tool" and st.get("state") == "ACTIVE":
            print("  🔧", st["tool_name"], json.dumps(st.get("tool_info", {}).get("parameters"), ensure_ascii=False)[:100])


def final_answer(stdout: str) -> tuple[str, str | None, bool]:
    """(câu trả lời, mã phiên, có_lỗi)"""
    for line in reversed(stdout.splitlines()):
        try:
            ev = json.loads(line)
        except ValueError:
            continue
        if ev.get("type") == "result":                                  # Claude
            return ev.get("result", ""), ev.get("session_id"), bool(ev.get("is_error"))
        if ev.get("event") == "result":                                 # Antigravity
            r = ev["result"]
            return r.get("response") or r.get("error") or "", r.get("conversation_id"), r.get("status") != "SUCCESS"
    return stdout[-500:], None, True


async def main(cli: str, cwd: str, prompt: str) -> None:
    argv, kind = command(cli)
    proc = await asyncio.create_subprocess_exec(*argv, cwd=cwd, creationflags=NO_WINDOW,
                                                stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
                                                stderr=asyncio.subprocess.PIPE)
    proc.stdin.write(payload(kind, prompt)); await proc.stdin.drain(); proc.stdin.close()
    out: list[bytes] = []

    async def pump():
        pending = b""
        while chunk := await proc.stdout.read(65536):
            out.append(chunk)
            *lines, pending = (pending + chunk).split(b"\n")
            for raw in lines:
                on_line(raw.decode("utf-8", "replace"))

    reader = asyncio.create_task(pump())
    err_task = asyncio.create_task(proc.stderr.read())
    try:
        await asyncio.wait_for(proc.wait(), 3600)
    except TimeoutError:
        subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"], capture_output=True)
    await asyncio.wait([reader, err_task], timeout=10)
    text, session, error = final_answer(b"".join(out).decode("utf-8", "replace").replace("\r\n", "\n"))
    stderr = err_task.result().decode("utf-8", "replace") if err_task.done() else ""
    if 'not found' in stderr and 'conversation' in stderr:
        print("⚠️ Antigravity không thấy phiên cũ, đã mở phiên mới")
    print("\n=== Trả lời ===\n" + text, "\nMã phiên:", session, "| Lỗi:", error)


if __name__ == "__main__":
    asyncio.run(main(*sys.argv[1:4]))
```

---

## 10. Xử lý sự cố

| Triệu chứng | Nguyên nhân | Cách xử lý |
|---|---|---|
| Gemini CLI báo `IneligibleTierError … migrate to the Antigravity suite` | Google chặn Gemini CLI với tài khoản cá nhân (free-tier) | Dùng Antigravity CLI (mục 5) hoặc `GEMINI_API_KEY` |
| `Please set an Auth method in … settings.json` (Gemini, code 41) | Gemini CLI chưa đăng nhập | `gemini` → Sign in with Google |
| Antigravity trả lời "How can I help you today?" không liên quan | Dùng `-p -`, CLI coi "-" là prompt | Dùng `--input-format stream-json` + tin JSON qua stdin, không dùng `-p` |
| Antigravity `status SUCCESS` mà `response` rỗng, có `denied_actions` | Agent chạy lệnh ở `--mode accept-edits`, bị chặn, lượt dừng | Dặn agent không chạy lệnh, hoặc dùng `--dangerously-skip-permissions`, hoặc thêm `permissions.allow` |
| Antigravity trả lời như chưa biết gì sau khi "mở lại phiên" | Cảnh báo `conversation "…" not found`, CLI tự mở phiên mới | Bắt cảnh báo trên stderr, chạy lại với prompt đầy đủ |
| `Eligibility check failed: UNAVAILABLE (code 503)` | Máy chủ Antigravity tạm lỗi | Chờ 15 giây rồi thử lại |
| Claude `No conversation found with session ID` | Phiên đã mất, hoặc là phiên của tài khoản hay nền tảng khác | Xoá mã phiên, chạy lại với prompt đầy đủ |
| Claude hết quota dù đã đổi tài khoản | Chương trình không biết đã đổi | So sánh `oauthAccount.accountUuid` trong `.claude.json` |
| Codex `401 Unauthorized … Missing bearer` | Chưa `codex login` | `codex login` |
| `winget install` lỗi `0x80072ee2` | Nguồn `msstore` hết thời gian chờ | Thêm `--source winget` |
| Tiến trình treo dù CLI đã thoát | Tiến trình con (dev server) giữ ống output | Chỉ chờ đọc tối đa 10 giây sau `proc.wait()` |
| Prompt tiếng Việt bị lỗi font hoặc mất ký tự | Truyền qua tham số `.cmd` / `cmd.exe` | Gửi qua stdin dạng UTF-8 |
| Bot hiểu nhầm là "hết quota" | Bắt chữ `rate limit`/`quota` đứng một mình trong output | Chỉ dùng mẫu chắc chắn; mẫu yếu chỉ khi CLI báo lỗi |

---

## 11. Checklist khi thêm một CLI mới

1. **Cài và lấy phiên bản**; ghi đường dẫn file chạy thật (`.exe` hay `.cmd`).
2. **Tìm chế độ chạy ngầm** (`-p`, `exec`, `--print`…) và **cách đưa prompt qua stdin**. Thử với prompt có tiếng Việt và ký tự `& | % "`.
3. **Định dạng kết quả:** có JSON hoặc stream-json không? Khoá nào chứa câu trả lời, mã phiên, lỗi, token, quota?
4. **Quyền:** cờ cho "chỉ sửa file" và "chạy mọi lệnh". Bị chặn thì CLI làm tiếp hay dừng lượt?
5. **Mở lại phiên:** cờ nào? ID sai thì báo lỗi hay lặng lẽ mở phiên mới?
6. **Lỗi:** chạy thử khi chưa đăng nhập, khi hết quota (nếu được), khi mất mạng. Ghi **nguyên văn** thông báo để làm mẫu nhận biết.
7. **Tách tài khoản:** biến môi trường nào chỉ tới thư mục cấu hình? Lưu tài khoản ở file nào?
8. **Tiến độ:** sự kiện nào cho biết đang dùng công cụ gì, với tham số nào?
9. Thêm vào `PROVIDERS`, `build_command`, `stdin_payload`, `parse_output`, `describe_event`, `account_info`, `COOLDOWN_MIN`. Nếu mở lại được phiên thì thêm vào `RESUMABLE`; nếu có tiến độ thì thêm vào `STREAMING`.
10. **Viết test** bằng output thật đã ghi lại (như `test_agy_parse.py`), rồi chạy thật một lần qua `Runner.probe` và một việc nhỏ.

---

## 12. Bảo mật và điều khoản

- Bot **chỉ gọi CLI chính thức** đã đăng nhập bằng tài khoản và gói của chủ máy, nên dùng đúng hạn mức của các gói đó.
- **Token Telegram** để trong `.env`, không commit (`.gitignore` đã có `.env` và `data/`). Token dán vào chat với AI thì đã nằm trong lịch sử, nên thu hồi bằng `/revoke` ở BotFather nếu cần.
- **⚡ Toàn quyền** (`bypassPermissions` / `--dangerously-skip-permissions` / `danger-full-access`) cho AI chạy mọi lệnh trên máy mà không hỏi. Chỉ bật cho dự án có git để quay lại. Thứ nằm ngoài thư mục dự án thì git không bảo vệ được.
- Mục 6 (`agentapi`) dùng thành phần nội bộ không có tài liệu: chỉ để tham khảo, **không đưa vào sản phẩm**.
