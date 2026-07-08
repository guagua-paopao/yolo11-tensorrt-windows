import argparse
import json
import os
import sys
import urllib.error
import urllib.request


def request_json(method, url, data=None, headers=None, timeout=30):
    req = urllib.request.Request(url, data=data, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read().decode("utf-8", errors="replace")
            return resp.status, json.loads(body)
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")
        try:
            return e.code, json.loads(body)
        except Exception:
            return e.code, {"raw": body}


def main():
    parser = argparse.ArgumentParser(description="Phase 13.5 invalid video input test")
    parser.add_argument("--url", default="http://127.0.0.1:8082")
    parser.add_argument("--payload", default=None, help="Optional file to upload as invalid video")
    args = parser.parse_args()

    if args.payload:
        with open(args.payload, "rb") as f:
            data = f.read()
    else:
        data = b"this is not a valid mp4 video\n" * 64

    code, body = request_json(
        "POST",
        f"{args.url}/api/v1/detect/video/async",
        data=data,
        headers={"Content-Type": "video/mp4"},
        timeout=60,
    )
    print("invalid-submit", code, json.dumps(body, ensure_ascii=False, indent=2)[:2000])

    if code == 400 and body.get("success") is False:
        print("PASS: invalid video was rejected by HTTP layer")
        return 0
    print("FAIL: invalid video should be rejected with HTTP 400")
    return 1


if __name__ == "__main__":
    sys.exit(main())
