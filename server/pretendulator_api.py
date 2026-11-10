#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
# SPDX-License-Identifier: GPL-3.0-or-later
"""
After Hours Arcade Manager's metadata API (deployed as pretendulator-api): looks games up on IGDB
with the project's Twitch account and keeps every answer and image, so each game reaches IGDB once
and the app never carries the account.

Python standard library only. Runs behind nginx, which serves images it already has from disk.

  GET /v1/lookup?name=<title>&platforms=508,130  the newest close match: {"game": {...}} or
                                                 {"game": null}
  GET /v1/search?q=<text>                        candidates: {"results": [...]}
  GET /v1/game/<igdb id>                         {"game": {...}} or {"game": null}
  GET /v1/platform-logos?ids=1,2                 {"logos": {"1": "<image id>", ...}}
  GET /img/<size>/<image id>.<jpg|png>           the image (fetched from IGDB once, then kept)

A game is {"id", "name", "summary", "year", "genres", "developer", "video_id", "cover", "hero"};
cover and hero are image ids for /img.

Environment: IGDB_CLIENT_ID, IGDB_CLIENT_SECRET (required); PRETENDULATOR_API_DATA (default
/var/lib/pretendulator-api); PRETENDULATOR_API_PORT (default 8090, on 127.0.0.1).
"""

import json
import os
import re
import sqlite3
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

CLIENT_ID = os.environ.get("IGDB_CLIENT_ID", "")
CLIENT_SECRET = os.environ.get("IGDB_CLIENT_SECRET", "")
DATA = Path(os.environ.get("PRETENDULATOR_API_DATA", "/var/lib/pretendulator-api"))
PORT = int(os.environ.get("PRETENDULATOR_API_PORT", "8090"))

DETAIL_FIELDS = ("fields name,summary,first_release_date,genres.name,involved_companies.developer,"
                 "involved_companies.company.name,cover.image_id,videos.video_id,"
                 "artworks.image_id,screenshots.image_id;")
IMAGE_SIZES = {"t_cover_small", "t_cover_big_2x", "t_1080p", "t_logo_med"}
IMAGE_ID = re.compile(r"^[a-z0-9]{1,40}$")
DAY = 24 * 3600
# Found games change rarely; a miss is retried sooner, as IGDB adds new releases.
FOUND_TTL = 30 * DAY
MISSING_TTL = 3 * DAY
SEARCH_TTL = 1 * DAY
USER_AGENT = "Pretendulator-API/1"


class Igdb:
    """IGDB requests, one at a time and at most four a second (IGDB's limit)."""

    def __init__(self):
        self.lock = threading.Lock()
        self.token = ""
        self.expiry = 0.0
        self.last = 0.0

    def _login(self):
        form = urllib.parse.urlencode({"client_id": CLIENT_ID, "client_secret": CLIENT_SECRET,
                                       "grant_type": "client_credentials"}).encode()
        with urllib.request.urlopen("https://id.twitch.tv/oauth2/token", form, timeout=20) as r:
            body = json.load(r)
        self.token = body["access_token"]
        self.expiry = time.time() + max(60, int(body.get("expires_in", 3600)) - 60)

    def query(self, endpoint, body):
        with self.lock:
            for attempt in range(2):
                if not self.token or time.time() >= self.expiry:
                    self._login()
                wait = self.last + 0.26 - time.time()
                if wait > 0:
                    time.sleep(wait)
                self.last = time.time()
                request = urllib.request.Request(
                    f"https://api.igdb.com/v4/{endpoint}", body.encode(),
                    {"Client-ID": CLIENT_ID, "Authorization": f"Bearer {self.token}",
                     "Content-Type": "text/plain", "User-Agent": USER_AGENT})
                try:
                    with urllib.request.urlopen(request, timeout=20) as r:
                        return json.load(r)
                except urllib.error.HTTPError as error:
                    if error.code == 401 and attempt == 0:
                        self.token = ""
                        continue
                    raise
        return []


class Cache:
    """Answers kept in SQLite: key -> (JSON, time saved, how long it stays good)."""

    def __init__(self, path):
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.lock = threading.Lock()
        with self.lock:
            self.db.execute("PRAGMA journal_mode=WAL")
            self.db.execute("CREATE TABLE IF NOT EXISTS answers (key TEXT PRIMARY KEY, "
                            "value TEXT NOT NULL, saved REAL NOT NULL, ttl REAL NOT NULL)")
            self.db.commit()

    def get(self, key):
        with self.lock:
            row = self.db.execute("SELECT value, saved, ttl FROM answers WHERE key = ?",
                                  (key,)).fetchone()
        if row and time.time() - row[1] < row[2]:
            return json.loads(row[0])
        return None

    def put(self, key, value, ttl):
        with self.lock:
            self.db.execute("INSERT OR REPLACE INTO answers VALUES (?, ?, ?, ?)",
                            (key, json.dumps(value), time.time(), ttl))
            self.db.commit()


igdb = Igdb()
cache = None


def clean_name(name):
    """Drops marks IGDB doesn't index and anything that could end the quoted search."""
    name = re.sub(r"[™®©\"\\;]", " ", name)
    return " ".join(name.split())[:120]


def normalized(name):
    return "".join(c for c in name.lower() if c.isalnum())


def release_year(game):
    seconds = game.get("first_release_date") or 0
    return datetime.fromtimestamp(seconds, timezone.utc).year if seconds > 0 else 0


def pick_newest_match(results, search_name):
    """The newest release with exactly the searched name, else the newest whose name contains it
    (or the reverse): a Switch 2 title is usually a new edition of a game with the same name."""
    wanted = normalized(search_name)
    best, best_date, best_exact = None, -1, False
    for game in results:
        name = normalized(game.get("name", ""))
        if not wanted or not name or (wanted not in name and name not in wanted):
            continue
        exact = name == wanted
        if best_exact and not exact:
            continue
        if exact and not best_exact:
            best_exact, best_date = True, -1
        date = game.get("first_release_date") or 0
        if date > best_date:
            best, best_date = game, date
    return best or results[0]


def simplify(game):
    """The fields the app shows, from an IGDB game with DETAIL_FIELDS."""
    developer = ""
    for company in game.get("involved_companies", []):
        if company.get("developer"):
            developer = company.get("company", {}).get("name", "")
            break
    hero = ""
    for field in ("artworks", "screenshots"):
        if game.get(field):
            hero = game[field][0].get("image_id", "")
            break
    videos = game.get("videos", [])
    return {
        "id": game.get("id", 0),
        "name": game.get("name", ""),
        "summary": game.get("summary", ""),
        "year": release_year(game),
        "genres": [genre.get("name", "") for genre in game.get("genres", [])],
        "developer": developer,
        "video_id": videos[0].get("video_id", "") if videos else "",
        "cover": game.get("cover", {}).get("image_id", ""),
        "hero": hero,
    }


def int_list(text, limit):
    values = []
    for part in (text or "").split(","):
        if part.strip().isdigit():
            values.append(int(part))
    return sorted(set(values))[:limit]


def lookup(name, platforms):
    name = clean_name(name)
    if not name:
        return {"game": None}
    key = f"lookup:{normalized(name)}:{','.join(map(str, platforms))}"
    cached = cache.get(key)
    if cached is not None:
        return cached
    results = []
    if platforms:
        results = igdb.query("games", f'search "{name}"; {DETAIL_FIELDS} '
                                      f'where platforms = ({",".join(map(str, platforms))}); limit 15;')
    if not results:
        results = igdb.query("games", f'search "{name}"; {DETAIL_FIELDS} limit 15;')
    answer = {"game": simplify(pick_newest_match(results, name)) if results else None}
    cache.put(key, answer, FOUND_TTL if results else MISSING_TTL)
    return answer


def search(text):
    text = clean_name(text)
    if not text:
        return {"results": []}
    key = f"search:{text.lower()}"
    cached = cache.get(key)
    if cached is not None:
        return cached
    games = igdb.query("games", f'search "{text}"; fields name,first_release_date,'
                                f'platforms.abbreviation,platforms.name,cover.image_id; limit 25;')
    results = [{
        "id": game.get("id", 0),
        "name": game.get("name", ""),
        "year": release_year(game),
        "platforms": ", ".join(p.get("abbreviation") or p.get("name", "")
                               for p in game.get("platforms", [])),
        "cover": game.get("cover", {}).get("image_id", ""),
    } for game in games]
    # Newest first: the Switch 2 edition of a game is usually its latest release.
    results.sort(key=lambda result: result["year"], reverse=True)
    answer = {"results": results}
    cache.put(key, answer, SEARCH_TTL)
    return answer


def game(igdb_id):
    key = f"game:{igdb_id}"
    cached = cache.get(key)
    if cached is not None:
        return cached
    games = igdb.query("games", f"{DETAIL_FIELDS} where id = {igdb_id};")
    answer = {"game": simplify(games[0]) if games else None}
    cache.put(key, answer, FOUND_TTL if games else MISSING_TTL)
    return answer


def platform_logos(ids):
    logos, missing = {}, []
    for platform in ids:
        cached = cache.get(f"logo:{platform}")
        if cached is None:
            missing.append(platform)
        elif cached:
            logos[str(platform)] = cached
    if missing:
        found = igdb.query("platforms", f"fields id,platform_logo.image_id; "
                                        f"where id = ({','.join(map(str, missing))}); limit 50;")
        images = {p["id"]: p.get("platform_logo", {}).get("image_id", "") for p in found}
        for platform in missing:
            cache.put(f"logo:{platform}", images.get(platform, ""), FOUND_TTL)
            if images.get(platform):
                logos[str(platform)] = images[platform]
    return {"logos": logos}


def image(size, image_id, extension):
    """Returns the image's bytes, downloading it from IGDB the first time."""
    path = DATA / "img" / size / f"{image_id}.{extension}"
    if path.exists():
        return path.read_bytes()
    request = urllib.request.Request(
        f"https://images.igdb.com/igdb/image/upload/{size}/{image_id}.{extension}",
        headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=30) as r:
        data = r.read()
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(f".{threading.get_ident()}.part")
    temporary.write_bytes(data)
    temporary.replace(path)
    return data


class Handler(BaseHTTPRequestHandler):
    server_version = "Pretendulator-API/1"

    def send(self, status, body, content_type="application/json", max_age=3600):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", f"public, max-age={max_age}")
        self.end_headers()
        self.wfile.write(body)

    def send_json(self, value, status=200):
        self.send(status, json.dumps(value, separators=(",", ":")).encode())

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(url.query)
        first = lambda name: query.get(name, [""])[0]
        try:
            if url.path == "/v1/lookup":
                return self.send_json(lookup(first("name"), int_list(first("platforms"), 10)))
            if url.path == "/v1/search":
                return self.send_json(search(first("q")))
            match = re.fullmatch(r"/v1/game/(\d{1,12})", url.path)
            if match:
                return self.send_json(game(int(match.group(1))))
            if url.path == "/v1/platform-logos":
                return self.send_json(platform_logos(int_list(first("ids"), 50)))
            match = re.fullmatch(r"/img/(t_[a-z0-9_]+)/([a-z0-9]+)\.(jpg|png)", url.path)
            if match and match.group(1) in IMAGE_SIZES and IMAGE_ID.match(match.group(2)):
                content_type = "image/png" if match.group(3) == "png" else "image/jpeg"
                return self.send(200, image(*match.groups()), content_type, 30 * DAY)
            if url.path == "/health":
                return self.send_json({"ok": True})
            self.send_json({"error": "not found"}, 404)
        except urllib.error.HTTPError as error:
            self.send_json({"error": f"IGDB answered {error.code}"},
                           404 if error.code == 404 else 502)
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            self.send_json({"error": f"IGDB unreachable: {error}"}, 502)

    def log_message(self, fmt, *args):
        # The real client address comes from nginx.
        client = self.headers.get("X-Real-IP", self.client_address[0])
        print(f"{client} {fmt % args}", flush=True)


def main():
    global cache
    if not CLIENT_ID or not CLIENT_SECRET:
        raise SystemExit("IGDB_CLIENT_ID and IGDB_CLIENT_SECRET must be set")
    DATA.mkdir(parents=True, exist_ok=True)
    cache = Cache(DATA / "cache.sqlite")
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    server.daemon_threads = True
    print(f"Listening on 127.0.0.1:{PORT}, data in {DATA}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
