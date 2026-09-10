#!/usr/bin/env python3
"""
zh_grep.py — fetch pages and print only the paragraphs that mention the
request-code story, so a Chinese forum thread fits in a paste.

    python3 tools/zh_grep.py URL [URL...]
    python3 tools/zh_grep.py --kw 请求码 --kw wudrm URL

Strips tags/scripts, splits into text blocks, keeps blocks matching any
keyword (default list below), prints up to --max blocks per page with the
page title and date-looking strings.
"""

import argparse
import html
import json
import re
import sys
import time
import urllib.parse
import urllib.request

DEFAULT_KW = ["请求码", "清单", "wudrm", "opensteamtool", "steam.run", "服务器", "封号",
              "账号", "关闭", "停止", "跑路", "无互联网", "manifest", "request code",
              "gmrc", "provider", "接口", "api"]
UA = ("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
      "Chrome/128.0 Safari/537.36")


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.5"})
    with urllib.request.urlopen(req, timeout=30) as r:
        raw = r.read()
        ct = r.headers.get("content-type", "")
    m = re.search(r"charset=([\w-]+)", ct, re.I)
    for enc in ([m.group(1)] if m else []) + ["utf-8", "gb18030"]:
        try:
            return raw.decode(enc)
        except Exception:
            continue
    return raw.decode("utf-8", "replace")


def to_blocks(doc):
    doc = re.sub(r"(?is)<(script|style|noscript).*?</\1>", " ", doc)
    title = re.search(r"(?is)<title>(.*?)</title>", doc)
    doc = re.sub(r"(?i)<br\s*/?>|</p>|</div>|</li>|</tr>|</h\d>|</blockquote>", "\n", doc)
    text = html.unescape(re.sub(r"(?s)<[^>]+>", " ", doc))
    blocks = [re.sub(r"[ \t\r\xa0]+", " ", b).strip() for b in text.split("\n")]
    return (html.unescape(title.group(1)).strip() if title else ""), [b for b in blocks if len(b) > 8]


def discourse_topic(url, maxlen=700):
    """Discourse forums (3a.lol, linux.do) render by JS; /t/<id>.json has the posts."""
    m = re.match(r"(https?://[^/]+)/t/(?:[^/]+/)?(\d+)", url)
    if not m:
        return False
    base, tid = m.group(1), m.group(2)
    try:
        d = json.loads(fetch(f"{base}/t/{tid}.json"))
    except Exception as e:
        print(f"  discourse fetch failed: {e}")
        return True
    print(f"  title: {d.get('title')}  posts: {d.get('posts_count')}  created: {str(d.get('created_at'))[:10]}")
    for p in d.get("post_stream", {}).get("posts", []):
        txt = re.sub(r"\s+", " ", html.unescape(re.sub(r"<[^>]+>", " ", p.get("cooked", "")))).strip()
        print(f"  - [{str(p.get('created_at'))[:10]} {p.get('username')}] {txt[:maxlen]}")
    return True


def discourse_search(base, query, n=20):
    q = urllib.parse.quote(query + " order:latest")
    try:
        d = json.loads(fetch(f"{base}/search.json?q={q}"))
    except Exception as e:
        print(f"  search failed: {e}")
        return
    for t in d.get("topics", [])[:n]:
        print(f"  {t['id']:>7}  {str(t.get('created_at'))[:10]}  {t.get('title')}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("urls", nargs="*")
    ap.add_argument("--search", action="append", default=[], metavar="BASE::QUERY",
                    help='Discourse search, e.g. "https://3a.lol::请求码"')
    ap.add_argument("--sleep", type=float, default=4, help="pause between requests (Discourse rate-limits)")
    ap.add_argument("--kw", action="append", default=[])
    ap.add_argument("--max", type=int, default=40)
    ap.add_argument("--all", action="store_true", help="print every block, not only matches")
    a = ap.parse_args()
    kws = [k.lower() for k in (a.kw or DEFAULT_KW)]
    for spec in a.search:
        base, _, query = spec.partition("::")
        print(f"\n===== search {base}: {query}")
        discourse_search(base, query)
        time.sleep(a.sleep)
    for url in a.urls:
        print(f"\n===== {url}")
        if discourse_topic(url):
            time.sleep(a.sleep)
            continue
        try:
            doc = fetch(url)
        except Exception as e:
            print(f"  FETCH FAILED: {e}")
            continue
        title, blocks = to_blocks(doc)
        dates = sorted(set(re.findall(r"20\d\d[-/年.]\d{1,2}[-/月.]\d{1,2}", doc)))[-6:]
        print(f"  title: {title}\n  dates seen: {', '.join(dates) or '-'}  blocks: {len(blocks)}")
        n = 0
        for b in blocks:
            if a.all or any(k in b.lower() for k in kws):
                print("  · " + b[:600])
                n += 1
                if n >= a.max:
                    print("  … (truncated, raise --max)")
                    break


if __name__ == "__main__":
    main()
