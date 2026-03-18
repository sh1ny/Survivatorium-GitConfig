# Survivatorium-GitConfig

**Open-source DayZ mod for syncing server configuration files from a private GitHub repository.**

---

## How It Works

```
  Your GitHub Repo          Proxy (localhost)          DayZ Server
  ┌──────────────┐         ┌──────────────┐          ┌───────────────┐
  │ serverFolder/ │◀─HTTPS─│  svc-gitconf │─HTTP/S─▶│               │
  │  ├─ profile/  │        │  ig-proxy    │          │  $profile:    │
  │  ├─ mission/  │        │  :8470       │          │  $mission:    │
  │  └─ saves/    │        └──────────────┘          │  $saves:      │
  └──────────────┘                                   └───────────────┘
```

On every server startup:

1. **Reads config** from `$profile:Survivatorium/GitConfig/config.json`
2. **Fetches the file tree** via the local proxy (single GitHub API call, lists all files + SHA hashes)
3. **Filters files** by server folder, target directory, extension whitelist, script blocking, size limit, and binary detection
4. **Compares SHA hashes** against the local cache — skips files that haven't changed on GitHub
5. **Downloads changed files** via the proxy with retry and response validation
6. **Binary/large files** are written directly to disk by the proxy (avoids engine text corruption)
7. **Empty marker files** (0-byte) are created locally without any network request
8. **Writes text files to disk** only if the response passes validation (not an error page, timeout, or HTML)

Files under `profile/` and `saves/` sync immediately at 3_Game init.
Files under `mission/` are deferred until the mission directory is available at 5_Mission init.

---

## Requirements

This mod requires the **Survivatorium-GitConfig-Proxy** — a lightweight Rust binary (~3 MB) that runs alongside your DayZ server. See the [Proxy](#proxy) section below.

DayZ's engine cannot connect to GitHub's API directly (missing `User-Agent` header, text-only HTTP responses). The proxy runs on `localhost`, accepts HTTP or HTTPS from the mod, and forwards requests to GitHub with proper headers. It also handles binary file downloads by writing them directly to disk.

The proxy supports **optional TLS** (`--tls-cert` / `--tls-key`), so traffic between DayZ and the proxy can be fully encrypted. See the proxy README for setup instructions.

---

## Setup

### Step 1: Create a GitHub Personal Access Token

1. Go to [GitHub Fine-Grained Tokens](https://github.com/settings/personal-access-tokens)
2. Click **Generate new token**
3. Set the token name (e.g., "DayZ Server Config Sync")
4. Under **Repository access**, select **Only select repositories** → choose your config repo
5. Under **Permissions → Repository permissions**, grant **Contents: Read-only**
6. Click **Generate token** and copy it

> **Security tip:** NEVER grant write access. Read-only is all this mod needs. Scope to a single repository.

### Step 2: Set Up Your GitHub Repository

Create a private repository with this folder structure:

```
your-repo/
└── {serverFolder}/             # e.g., "Survivatorium_DeerIsle_v5"
    ├── profile/                # → synced to $profile:
    │   ├── ExpansionMod/
    │   │   └── Settings/
    │   │       └── GeneralSettings.json
    │   ├── TerjeSettings/
    │   │   └── Core.cfg
    │   └── ...
    ├── mission/                # → synced to $mission: (opt-in)
    │   ├── cfggameplay.json
    │   ├── cfgspawnabletypes.xml
    │   ├── areaflags.map
    │   ├── db/
    │   │   └── types.xml
    │   └── ...
    └── saves/                  # → synced to $saves: (opt-in)
        └── ...
```

The folder mapping is straightforward:
- `{serverFolder}/profile/X` → `$profile:X`
- `{serverFolder}/mission/X` → `$mission:X`
- `{serverFolder}/saves/X` → `$saves:X`

### Step 3: Install the Mod

Add `Survivatorium-GitConfig` to your server's `-mod=` list.

### Step 4: Set Up the Proxy

See the [Proxy](#proxy) section for build and startup instructions.

### Step 5: Configure

On first launch, the mod creates a default config at:
```
$profile:Survivatorium/GitConfig/config.json
```

Edit it with your details:
```json
{
    "version": 1,
    "githubToken": "github_pat_xxxxxxxxxxxx",
    "repoOwner": "your-github-username",
    "repoName": "your-repo-name",
    "branch": "main",
    "serverFolder": "Survivatorium_DeerIsle_v5",
    "proxyUrl": "http://127.0.0.1:8470",
    "enableProfileSync": true,
    "enableMissionSync": false,
    "enableSavesSync": false,
    "maxRetries": 3,
    "maxFileSizeMB": 50,
    "abortOnTreeFetchFail": true,
    "blockScriptFiles": true,
    "permitAllExtensions": false,
    "proxyWriteThresholdKB": 10240,
    "forceRedownload": false,
    "binaryExtensions": [".dze", ".map", ".bin", ".pbo", ".pak"],
    "allowedExtensions": [".json", ".xml", ".cfg", ".txt", ".csv", ".map"],
    "lastFileHashes": {}
}
```

### Step 6: Restart

Restart your server. Check the RPT log for `[SVC-GitConfig]` messages to confirm sync is working.

---

## Configuration Reference

| Field | Default | Description |
|---|---|---|
| `githubToken` | `""` | Your GitHub Fine-Grained PAT (read-only, single repo) |
| `repoOwner` | `""` | GitHub username or organization |
| `repoName` | `""` | Repository name |
| `branch` | `"main"` | Git branch to sync from |
| `serverFolder` | `"default"` | Subfolder in the repo that contains this server's files |
| `proxyUrl` | `"http://127.0.0.1:8470"` | URL of the Survivatorium-GitConfig-Proxy |
| `enableProfileSync` | `true` | Sync `profile/` → `$profile:` |
| `enableMissionSync` | `false` | Sync `mission/` → `$mission:` **(see Mission Sync below)** |
| `enableSavesSync` | `false` | Sync `saves/` → `$saves:` |
| `maxRetries` | `3` | Download attempts per file before giving up |
| `maxFileSizeMB` | `50` | Skip files larger than this (MB). `0` = no limit |
| `abortOnTreeFetchFail` | `true` | Stop entirely if the GitHub file listing fails |
| `blockScriptFiles` | `true` | Block `.c` (EnScript) files **(see Script Blocking below)** |
| `permitAllExtensions` | `false` | Bypass the extension whitelist entirely — download all file types |
| `proxyWriteThresholdKB` | `10240` | Files above this size (KB) use proxy-write instead of in-engine download |
| `forceRedownload` | `false` | Re-download all files ignoring cache. Auto-resets to `false` after sync |
| `binaryExtensions` | `[".dze", ...]` | File extensions that always use proxy-write (binary-safe disk write) |
| `allowedExtensions` | `[".json", ...]` | Only files matching these extensions are downloaded |
| `lastFileHashes` | `{}` | SHA cache — **do not edit manually** (use `forceRedownload` instead) |

---

## Proxy

The proxy is a self-contained Rust binary that bridges DayZ's HTTP engine to GitHub's API. It runs on `localhost` alongside your DayZ server.

### Why is a proxy needed?

DayZ's Enfusion engine has limited HTTP capabilities:
- Cannot set custom HTTP headers (only `Content-Type` via `SetHeader`)
- GitHub requires a `User-Agent` header and rejects all requests without one
- Text-based `GET_now()` corrupts binary files and times out on large downloads

The proxy solves all three:
- Adds proper `User-Agent`, `Authorization`, and `Accept` headers to GitHub requests
- Downloads binary/large files and writes them directly to disk
- Runs only on localhost — your token never leaves the machine except over TLS to GitHub

### Endpoints

| Endpoint | Purpose |
|----------|---------|
| `GET /tree` | Fetch the full repository file tree from GitHub |
| `GET /raw` | Download a single file's content as text |
| `GET /write` | Download a file from GitHub and write it directly to disk (binary-safe) |
| `GET /health` | Health check |

### Build from Source

```powershell
# Install Rust if needed: https://rustup.rs
cd Survivatorium-GitConfig-Proxy
cargo build --release
```

The binary will be at `target\release\svc-gitconfig-proxy.exe` (~3 MB).

### Run

```powershell
$env:GITHUB_TOKEN = "github_pat_XXXX"
.\svc-gitconfig-proxy.exe --bind 127.0.0.1 --port 8470 `
    --profile-path "C:\DayZServer\profiles" `
    --mission-path "C:\DayZServer\mpmissions\dayzOffline.chernarusplus"
```

| Argument | Default | Description |
|---|---|---|
| `--port` | `8470` | Port to listen on |
| `--bind` | `127.0.0.1` | Bind address (`0.0.0.0` for external) |
| `--token` / `GITHUB_TOKEN` | — | GitHub PAT (overrides token from mod) |
| `--profile-path` / `SVC_PROFILE_PATH` | — | Local path to DayZ `$profile:` directory (enables `/write` for profile files) |
| `--mission-path` / `SVC_MISSION_PATH` | — | Local path to DayZ `$mission:` directory (enables `/write` for mission files) |
| `--timeout` | `300` | HTTP timeout in seconds for GitHub downloads |
| `--allowed-ips` / `SVC_ALLOWED_IPS` | `127.0.0.1` | Comma-separated client IP allowlist. Use `0.0.0.0` to allow all. |
| `--tls-cert` / `SVC_TLS_CERT` | — | Path to TLS certificate (PEM). Enables HTTPS. |
| `--tls-key` / `SVC_TLS_KEY` | — | Path to TLS private key (PEM). Enables HTTPS. |

> **Important:** `--profile-path` and `--mission-path` are required for the `/write` endpoint (binary and large file downloads). Without them, only text-based `/raw` downloads work.

### Helper Script

Edit `start_proxy.bat`, set your `GITHUB_TOKEN` and paths, then run it:

```bat
@echo off
set GITHUB_TOKEN=PASTE_YOUR_GITHUB_PAT_HERE
set SVC_PROFILE_PATH=C:\DayZServer\profiles
set SVC_MISSION_PATH=C:\DayZServer\mpmissions\dayzOffline.chernarusplus

svc-gitconfig-proxy.exe --bind 127.0.0.1 --port 8470
```

### Docker

```bash
docker build -t svc-gitconfig-proxy .
docker run -d \
    -e GITHUB_TOKEN=github_pat_XXXX \
    -e SVC_PROFILE_PATH=/data/profiles \
    -e SVC_MISSION_PATH=/data/mission \
    -v /path/to/profiles:/data/profiles \
    -v /path/to/mission:/data/mission \
    -p 8470:8470 \
    svc-gitconfig-proxy --bind 0.0.0.0
```

### Platform Support

- **Windows** — Server 2016, 2019, 2022, Windows 10/11 (x86_64)
- **Linux** — Build from source on the target machine, or use the Dockerfile
- No runtime dependencies — the binary is fully self-contained (bundled TLS via rustls)

---

## Security

This mod implements multiple layers of protection. Each can be configured independently.

### Layer 1: Directory Sync Toggles

Each target directory has its own toggle. Only `profile/` is enabled by default:

| Directory | Setting | Default | Risk Level |
|---|---|---|---|
| `profile/` | `enableProfileSync` | **true** | Low — server config files |
| `mission/` | `enableMissionSync` | **false** | **High** — can contain server scripts |
| `saves/` | `enableSavesSync` | **false** | Medium — save game data |

### Layer 2: Extension Whitelist (`allowedExtensions`)

Only files with whitelisted extensions are downloaded. Default list:

```
.json  .xml  .cfg  .txt  .csv  .map
```

- Files with unlisted extensions are silently skipped (logged in RPT)
- Use `"permitAllExtensions": true` to bypass the whitelist entirely

### Layer 3: Script File Blocking (`blockScriptFiles`)

**This is a hard safety gate, separate from the extension whitelist.**

When `blockScriptFiles` is `true` (default):
- **All `.c` files are blocked**, regardless of `allowedExtensions` or `permitAllExtensions`
- This is the safe default

When `blockScriptFiles` is `false`:
- `.c` files are treated like any other file — subject to the extension whitelist
- You still need to add `".c"` to `allowedExtensions` (or enable `permitAllExtensions`)
- **This is a two-key system** — you must change both settings to allow scripts

#### When to unblock `.c` files

You need script syncing if your repo contains files like:
- `profile/ExpansionMod/AI/FSM/Master.c` — AI behavior trees
- `mission/init.c` — custom server initialization

**To enable:**

1. Set `"blockScriptFiles": false`
2. Add `".c"` to `allowedExtensions` (or set `"permitAllExtensions": true`)
3. If the files are under `mission/`, also set `"enableMissionSync": true`

**Only do this if:**
- Your GitHub account has 2FA enabled
- Your PAT is **read-only** (no write access to the repo)
- You are the only person with write access to the repository

### Layer 4: File Size Limit (`maxFileSizeMB`)

Files exceeding the size limit are skipped before download begins. Default: 50 MB.

- The GitHub Trees API provides file sizes, so the check happens **before** any download
- Set to `0` to disable the limit

### Layer 5: Response Validation

Every text-downloaded file is validated before writing to disk. The mod **rejects** responses that match:

- `"Server Error"` — GitHub 5xx responses
- `"Client Error"` — GitHub 4xx responses
- `"Timeout"` — engine-level request timeout
- `"404: Not Found"` — missing files
- `"<!DOCTYPE"` / `"<html"` — HTML error pages

If validation fails, the mod retries up to `maxRetries` times. If all attempts fail, **the file is not written** — your existing local config stays intact.

### Layer 6: Path Traversal Protection

- File paths containing `..` are automatically blocked
- Config values (`repoOwner`, `repoName`, `branch`, `serverFolder`) are validated for unsafe characters
- The proxy also validates paths and checks that resolved write targets stay within the configured base directories

### Layer 7: Binary File Handling

Binary files (`.dze`, `.map`, `.bin`, `.pbo`, `.pak` by default) are routed through the proxy's `/write` endpoint, which downloads from GitHub and writes raw bytes directly to disk. This avoids:
- Text encoding corruption from DayZ's `GET_now()` method
- Engine timeouts on large file downloads
- Truncation of binary content

Files above `proxyWriteThresholdKB` (default 10 MB) are also automatically routed through `/write`, regardless of extension.

### Token Security

- The proxy supports optional TLS (`--tls-cert` / `--tls-key`), encrypting all traffic between DayZ and the proxy
- When TLS is enabled, the token is encrypted end-to-end: DayZ ─HTTPS─▶ Proxy ─HTTPS─▶ GitHub
- Without TLS, the token travels in plaintext HTTP between DayZ and the proxy (acceptable on localhost, risky over a network)
- The proxy runs on `localhost` only by default — the token never leaves the machine except over TLS to GitHub
- **The token is never printed to the RPT log**

---

## Features

### SHA Caching

The mod caches the SHA hash of every downloaded file. On subsequent boots, only files that changed on GitHub are re-downloaded. This makes restarts fast — typically a few seconds even with hundreds of config files.

### Force Re-download

Set `"forceRedownload": true` in config.json to clear all cached hashes and re-download everything. The flag automatically resets to `false` after sync completes — no manual cleanup needed.

### Empty Marker Files

Some mods use 0-byte files as setup flags (e.g., `.initial_setup_complete`). These are detected via the tree API and created directly on disk without any network request.

### Deferred Mission Files

Files targeting `$mission:` are queued during initial sync and processed after the mission directory becomes available at `5_Mission` init.

### Permit All Extensions

Set `"permitAllExtensions": true` to bypass the extension whitelist entirely. All file types will be downloaded (subject to script blocking and size limits). Useful when your repo contains many different file types.

---

## Troubleshooting

### "Fetching file tree via proxy" then nothing

- Verify the proxy is running: `curl http://127.0.0.1:8470/health` should return `OK`
- Check that `proxyUrl` in config.json matches the proxy's bind address and port

### "Request timed out"

- The file may be too large for DayZ's text-based HTTP client
- Add the file extension to `binaryExtensions`, or lower `proxyWriteThresholdKB`
- For 0-byte files, they are now handled automatically as empty marker files

### "FAILED proxy-write"

- Verify the proxy was started with `--profile-path` and/or `--mission-path`
- Check that the paths are correct and the proxy has write access

### Files not downloading

Check the RPT log for `Skipped` messages and verify:

| Issue | Fix |
|---|---|
| Wrong extension | Add the extension to `allowedExtensions` or set `permitAllExtensions: true` |
| `.c` file blocked | Set `blockScriptFiles: false` AND add `.c` to `allowedExtensions` |
| Directory disabled | Set `enableMissionSync` / `enableSavesSync` to `true` |
| File too large | Increase `maxFileSizeMB` or set to `0` |
| Binary file corrupted | Add the extension to `binaryExtensions` |

### "FAILED after X attempts"

The mod validates every response before writing. If the proxy or GitHub returns an error, it retries up to `maxRetries` times. If all fail, the file is **not written** to prevent corrupting your config.

Common causes:
- Proxy not running or crashed — restart it
- GitHub rate limiting — unlikely with SHA caching, but possible with many servers sharing one token
- File path encoding issue — check the proxy logs for details

### "WARNING: GitHub truncated the tree response"

Your repo has too many files. GitHub's Tree API truncates at ~100,000 entries. Consider splitting configs into a dedicated repo.

---

## License

MIT — Use it, fork it, improve it. See [LICENSE](LICENSE).
