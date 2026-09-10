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
import re
import sys
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("urls", nargs="+")
    ap.add_argument("--kw", action="append", default=[])
    ap.add_argument("--max", type=int, default=40)
    ap.add_argument("--all", action="store_true", help="print every block, not only matches")
    a = ap.parse_args()
    kws = [k.lower() for k in (a.kw or DEFAULT_KW)]
    for url in a.urls:
        print(f"\n===== {url}")
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
