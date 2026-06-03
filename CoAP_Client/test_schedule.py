#!/usr/bin/env python3
"""
Test suite for Schedule (m2m:sch) CRUD operations.

URL contract (added to main.c):
  POST   http://HOST/CSE/AE               body: {"m2m:sch": {"rn": ..., "sce": ...}}
  GET    http://HOST/CSE/AE/SCH_RN?ty=29
  PUT    http://HOST/CSE/AE/SCH_RN?ty=29  body: {"m2m:sch": {}}
  DELETE http://HOST/CSE/AE/SCH_RN?ty=29

Usage:
  python3 test_schedule.py [HOST]         # default: 127.0.0.1:8080
"""

import sys
import time
import requests

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:8080"
BASE = f"http://{HOST}"
CSE  = "mn-name"     # must match the CSEBase rn configured on the server
AE   = "schedTestAE"
SCH  = "mySched"

GREEN = "\033[92m"
RED   = "\033[91m"
RESET = "\033[0m"

passed = 0
failed = 0


def check(label: str, resp: requests.Response, expected: int) -> requests.Response:
    global passed, failed
    ok = resp.status_code == expected
    tag = f"{GREEN}[PASS]{RESET}" if ok else f"{RED}[FAIL]{RESET}"
    print(f"{tag}  {label:48s}  HTTP {resp.status_code}  (expected {expected})")
    if not ok:
        failed += 1
        print(f"       body: {resp.text[:160].replace(chr(10), ' ')}")
    else:
        passed += 1
    return resp


# ─── Setup ────────────────────────────────────────────────────────────────────

print(f"\nServer : {BASE}")
print(f"CSEBase: {CSE}   AE: {AE}   Schedule rn: {SCH}\n")
print("=== Setup ===")

r = requests.post(f"{BASE}/{CSE}", json={
    "m2m:ae": {
        "rn": AE,
        "api": "N01.com.schedtest",
        "rr": True,
        "srv": ["3"],
        "poa": [BASE]
    }
})
if r.status_code not in (201, 403):
    print(f"[WARN] AE setup returned {r.status_code} — tests may fail: {r.text[:80]}")
else:
    print(f"  AE '{AE}' ready (HTTP {r.status_code})")

# Clean any leftover schedule from a previous run
requests.delete(f"{BASE}/{CSE}/{AE}/{SCH}?ty=29")

# ─── CRUD Tests ───────────────────────────────────────────────────────────────

print("\n=== Schedule CRUD Tests ===")

# 1. Correct POST → 201
r1 = check(
    "POST valid schedule → 201",
    requests.post(f"{BASE}/{CSE}/{AE}", json={
        "m2m:sch": {
            "rn": SCH,
            "sce": "30 9 * * 1-5"      # weekdays at 09:30
        }
    }),
    201
)

ri_from_post = None
if r1.status_code == 201:
    try:
        ri_from_post = r1.json()["m2m:sch"]["ri"]
        print(f"       ri: {ri_from_post}")
    except (KeyError, ValueError):
        pass

# 2. Duplicate POST (same rn) → 403
check(
    "POST duplicate name → 403",
    requests.post(f"{BASE}/{CSE}/{AE}", json={
        "m2m:sch": {
            "rn": SCH,
            "sce": "30 9 * * 1-5"
        }
    }),
    403
)

# 3. POST with invalid sce format → 400
check(
    "POST invalid sce format → 400",
    requests.post(f"{BASE}/{CSE}/{AE}", json={
        "m2m:sch": {
            "rn": "badSched",
            "sce": "not-cron"
        }
    }),
    400
)

# 4. POST missing mandatory fields → 400
check(
    "POST missing mandatory fields → 400",
    requests.post(f"{BASE}/{CSE}/{AE}", json={"m2m:sch": {}}),
    400
)

# 5. GET existing schedule → 200 + validate payload
r5 = check(
    "GET existing schedule → 200",
    requests.get(f"{BASE}/{CSE}/{AE}/{SCH}?ty=29"),
    200
)
if r5.status_code == 200:
    body = r5.json()
    assert "m2m:sch" in body,                        "Missing m2m:sch key in response"
    assert body["m2m:sch"]["rn"]  == SCH,            "Wrong rn in response"
    assert body["m2m:sch"]["sce"] == "30 9 * * 1-5", "Wrong sce in response"
    assert "ri" in body["m2m:sch"],                  "Missing ri field"
    assert "ct" in body["m2m:sch"],                  "Missing ct field"
    print(f"       payload ok — ri={body['m2m:sch']['ri']}, sce={body['m2m:sch']['sce']}")

# 6. GET non-existent schedule → 404
check(
    "GET non-existent schedule → 404",
    requests.get(f"{BASE}/{CSE}/{AE}/doesNotExist?ty=29"),
    404
)

# 7. PUT existing schedule → 200 (updates lt)
r7 = check(
    "PUT existing schedule → 200",
    requests.put(f"{BASE}/{CSE}/{AE}/{SCH}?ty=29", json={"m2m:sch": {}}),
    200
)
if r5.status_code == 200 and r7.status_code == 200:
    lt_before = r5.json()["m2m:sch"].get("lt", "")
    time.sleep(1)
    r7b = requests.get(f"{BASE}/{CSE}/{AE}/{SCH}?ty=29")
    if r7b.status_code == 200:
        lt_after = r7b.json()["m2m:sch"].get("lt", "")
        if lt_before and lt_after and lt_before != lt_after:
            print(f"       lt updated: {lt_before} → {lt_after}")
        else:
            print(f"       [NOTE] lt unchanged (PUT updates lt at second resolution)")

# 8. DELETE existing schedule → 200
check(
    "DELETE existing schedule → 200",
    requests.delete(f"{BASE}/{CSE}/{AE}/{SCH}?ty=29"),
    200
)

# 9. DELETE already-deleted schedule → 404
check(
    "DELETE non-existent schedule → 404",
    requests.delete(f"{BASE}/{CSE}/{AE}/{SCH}?ty=29"),
    404
)

# 10. GET after DELETE → 404
check(
    "GET after DELETE → 404",
    requests.get(f"{BASE}/{CSE}/{AE}/{SCH}?ty=29"),
    404
)

# ─── Teardown ─────────────────────────────────────────────────────────────────

print("\n=== Teardown ===")
r = requests.delete(f"{BASE}/{CSE}/{AE}")
print(f"  AE '{AE}' removed (HTTP {r.status_code})")

# ─── Summary ──────────────────────────────────────────────────────────────────

total = passed + failed
print(f"\n{'─'*55}")
print(f"  Passed: {passed}/{total}    Failed: {failed}/{total}")
if failed == 0:
    print(f"{GREEN}  All tests passed.{RESET}")
else:
    print(f"{RED}  {failed} test(s) failed.{RESET}")
    sys.exit(1)
