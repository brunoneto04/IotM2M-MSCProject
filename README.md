# IotM2M — MSC Project

Project on **IoT / M2M** built around a [**oneM2M**](https://www.onem2m.org/)
**CSE** (Common Services Entity) server written in **C**, with a **SQLite** backend
and support for three protocols — **HTTP**, **CoAP** and **MQTT**.

The main deliverable is the CSE server in [`IoT-m2m/`](IoT-m2m/). The remaining
folders provide supporting material: Python test clients and an early standalone
HTTP experiment.

---

## Table of contents

1. [Repository layout](#repository-layout)
2. [Architecture overview](#architecture-overview)
3. [Resource model](#resource-model)
4. [Database schema](#database-schema)
5. [Components](#components)
6. [Quick start](#quick-start)
7. [API examples](#api-examples)
8. [Schedule → Action automation](#schedule--action-automation)
9. [Testing](#testing)
10. [Requirements](#requirements)
11. [Documentation index](#documentation-index)
12. [Notes & known limitations](#notes--known-limitations)

---

## Repository layout

```
IotM2M-MSCProject/
├── IoT-m2m/            # ★ Main oneM2M CSE server (C) — HTTP + CoAP + MQTT
│   ├── main.c / main.h         # Startup, HTTP/CoAP routing, threads
│   ├── makefile
│   ├── handlers/               # Per-resource logic (one module per resource)
│   ├── include/                # Public headers
│   ├── database/               # SQL schema + generated SQLite DB
│   ├── READ_ME/                # Reference notes (API requests, VM setup, legacy SQL)
│   ├── README.md               # Full build/run/test guide for the server
│   └── SCHEDULE_ACTION_INTEGRATION.md   # Schedule→Action deep-dive
├── CoAP_Client/        # Python test clients (aiocoap) + Schedule HTTP test suite
└── HTTP-Server-c/      # Minimal standalone HTTP server experiment (port 8069)
```

---

## Architecture overview

The CSE server (`IoT-m2m/`) is a single process that, on startup, creates the
CSEBase and launches a set of background threads:

| Thread | Source | Role |
|--------|--------|------|
| HTTP server | `start_web_server` (`main.c`) | Listens on TCP **8080**, parses requests, routes to handlers |
| CoAP server | `start_coap_server` (`main.c`) | Listens on UDP **5683** via libcoap, single unknown-resource handler |
| Expiry cleanup | `check_and_delete_expired_resources` (`main.c`) | Periodically deletes resources past their `et` (expirationTime) |
| Scheduler | `scheduler_thread_func` (`handlers/schedule.c`) | Wakes every **60 s**, evaluates Schedule cron expressions, triggers Actions |

The MQTT client (`handlers/mqtt_client.c`, paho) delivers **subscription
notifications** over an external broker (default MQTT port **1883**), alongside HTTP
notifications.

```
            ┌──────────── HTTP :8080 ───────────┐
 clients ──▶│                                   │
            ├──────────── CoAP :5683 ───────────┤──▶  request router ──▶ resource handlers ──▶ SQLite
            │                                   │                                                (iotm2m.db)
            └─ MQTT :1883 (notifications out) ◀─┘
                                                          ▲
                          scheduler thread (60 s) ────────┘  (internal Schedule→Action trigger)
```

> Each resource type maps to its own `.c`/`.h` module under `handlers/` and
> `include/`. Persistence is shared: a generic `resources` table holds the common
> attributes, and one table per resource type holds the specifics (see
> [Database schema](#database-schema)).

---

## Resource model

Resources form the standard oneM2M containment tree, rooted at the CSEBase:

```
CSEBase (mn-name)
└── AE (m2m:ae)
    ├── Container (m2m:cnt)
    │   ├── ContentInstance (m2m:cin)
    │   ├── Subscription (m2m:sub)
    │   └── Action (m2m:act)
    ├── Schedule (m2m:sch)
    └── Subscription (m2m:sub)
```
## Database schema

SQLite database at `IoT-m2m/database/iotm2m.db`, schema in
`IoT-m2m/database/create_tables.sql`. A generic `resources` table stores the common
attributes (`ty`, `ri`, `rn`, `pi`, `ct`, `lt`) with a self-referencing `pi → ri`
foreign key; specialised tables extend each resource:

| Table | Holds |
|-------|-------|
| `resources` | Common attributes for every resource (the containment tree) |
| `cse_bases` | `csi`, `cst` |
| `application_entities` | `api`, `aei`, `rr`, `et` |
| `containers` | `st`, `cni`, `cbs`, `mbs`, `mia`, `mni`, `et` |
| `content_instances` | `con`, `cs`, `st`, `et` |
| `subscriptions` | `notification_uris`, `notification_type`, `event_type`, … |
| `actions` | `eval_criteria_*`, `eval_mode`, `object_resource_id`, `action_primitive`, `input`, … |
| `schedules` | `sce` (cron expression), `et` |
| `points_of_access`, `supported_resource_types`, `access_control_policy_ids`, `content_serializations`, `supported_release_versions`, `labels` | Multi-valued attributes (one row per value) |

The server verifies/creates all tables on startup. To reset manually:

```bash
cd IoT-m2m
sqlite3 database/iotm2m.db < database/create_tables.sql   # (re)create schema
sqlite3 database/iotm2m.db ".schema"                      # inspect
```

> A fresh clone may ship an outdated `iotm2m.db`; regenerate it from
> `create_tables.sql` so the `actions`/`schedules` tables exist before use.

---

## Components

### 1. `IoT-m2m/` — oneM2M CSE server ★

The core deliverable. A multi-threaded oneM2M CSE server in C with a SQLite
backend. See **[`IoT-m2m/README.md`](IoT-m2m/README.md)** for the complete
dependency/build/run/test guide. Highlights:

- Full CRUD for the resource types above, persisted in SQLite.
- **Subscriptions** with HTTP and MQTT notification delivery.
- **Schedule** resource with a 5-field Unix cron `sce` and a 60 s evaluation thread.
- **Action** resource with CRUD plus a **Schedule → Action** execution bridge.
- Automatic expired-resource cleanup.
- Compile-time toggles per protocol (`ENABLE_HTTP`, `ENABLE_COAP`, `ENABLE_MQTT`)
  and logging (`ENABLE_LOGGING=1`).

**Default ports:** HTTP `8080`, CoAP/UDP `5683`, MQTT `1883` (external broker).

### 2. `CoAP_Client/` — test clients

Python clients used to exercise the running server:

| File | Protocol | Purpose |
|------|----------|---------|
| `client_test_AE.py` | CoAP | Create/inspect an AE |
| `client_test_Container.py` | CoAP | Create/inspect a Container |
| `client_test_ContentInstance.py` | CoAP | Create/inspect ContentInstances |
| `test_schedule.py` | HTTP | Full Schedule CRUD test suite |
| `define_ip.py` | — | Sets the target server IP for the CoAP clients |

CoAP clients require [`aiocoap`](https://aiocoap.readthedocs.io/); the Schedule
suite requires `requests`. Set the server IP in `define_ip.py` before running the
CoAP clients.

### 3. `HTTP-Server-c/` — minimal HTTP server experiment

A standalone, single-file C HTTP server (`http-server.c`) that always replies
`200 OK`. An early proof of concept, **independent** of the CSE server. Listens on
port **8069**.

```bash
cd HTTP-Server-c
gcc http-server.c -o http-server
./http-server
```

---

## Quick start

```bash
# 1. Build the CSE server (with logs, recommended for development)
cd IoT-m2m
make ENABLE_LOGGING=1

# 2. (Optional) (re)create the database schema
sqlite3 database/iotm2m.db < database/create_tables.sql

# 3. Run — you will be prompted for the CSEBase resourceName (default: mn-name)
./iotm2m
```

On startup the server creates the CSEBase, then starts the HTTP (8080), CoAP
(5683), expiry-cleanup and scheduler threads. Stop it cleanly with `Ctrl+C`
(handles `SIGINT`/`SIGTERM` and joins the scheduler thread).

Full dependency and build details (libcoap, paho-mqtt, build flags, valgrind, etc.):
**[`IoT-m2m/README.md`](IoT-m2m/README.md)**.

---

## API examples

Assuming `HOST=127.0.0.1:8080` and the default CSEBase `mn-name`:

```bash
HOST=127.0.0.1:8080
CSE=mn-name

# Create an AE
curl -s -X POST http://$HOST/$CSE \
  -H "Content-Type: application/json;ty=2" -H "X-M2M-Origin: CAdmin" \
  -d '{"m2m:ae":{"rn":"sensorAE","api":"N01.com.test","rr":true,"srv":["3"],"poa":["http://127.0.0.1:8080"]}}'

# Create a Container under the AE
curl -s -X POST http://$HOST/$CSE/sensorAE \
  -H "Content-Type: application/json;ty=3" \
  -d '{"m2m:cnt":{"rn":"temp","mni":100,"mbs":100000}}'

# Add a ContentInstance (data point)
curl -s -X POST http://$HOST/$CSE/sensorAE/temp \
  -H "Content-Type: application/json;ty=4" \
  -d '{"m2m:cin":{"con":"21.5"}}'

# Read the latest ContentInstance
curl -s "http://$HOST/$CSE/sensorAE/temp/la?ty=4"

# Create a Schedule under the AE (weekdays at 09:30)
curl -s -X POST http://$HOST/$CSE/sensorAE \
  -H "Content-Type: application/json" \
  -d '{"m2m:sch":{"rn":"mySched","sce":"30 9 * * 1-5"}}'
```

More request/response examples (headers, status codes, full bodies) are in
[`IoT-m2m/READ_ME/API Requests.txt`](IoT-m2m/READ_ME/).

---

## Schedule → Action automation

The standout feature: a **Schedule** can trigger an **Action** at a given time —
*"at this time, run this action"*.

- A **Schedule** holds a 5-field Unix cron expression in `sce`
  (`minute hour day-of-month month day-of-week`; supports `*`, lists `1,3`,
  ranges `1-5`, steps `*/2`).
- An **Action** describes what to do — currently the `CREATE` primitive
  creates a `<contentInstance>` (`con = inp`) under its target (`ori`).
- The scheduler thread evaluates every schedule once per minute; on a match it
  triggers the actions living under the **containers of the schedule's AE** (linked
  by shared ancestry).

---

## Testing

```bash
# Schedule HTTP suite (server must be running)
pip3 install requests
python3 CoAP_Client/test_schedule.py [HOST]      # default HOST: 127.0.0.1:8080
# Covers: POST 201, duplicate POST 403, GET 200/404, PUT 200, DELETE 200/404

# CoAP clients (set the server IP in CoAP_Client/define_ip.py first)
pip3 install aiocoap
python3 CoAP_Client/client_test_AE.py
python3 CoAP_Client/client_test_Container.py
python3 CoAP_Client/client_test_ContentInstance.py

# Memory checks
cd IoT-m2m && valgrind --leak-check=full ./iotm2m
```

To exercise the scheduler quickly, create a schedule that fires in the current
minute (`sce` of `"* * * * *"`) and watch the logs (built with `ENABLE_LOGGING=1`)
for `[Scheduler] Schedule matched (...)`.

---

## Requirements

- Ubuntu 22.04 / 24.04 (or WSL2 with Ubuntu 24.04)
- GCC 13+, CMake 3.16+ (CMake only needed if building libcoap / paho-mqtt from source)
- Libraries: `libsqlite3-dev`, `libjson-c-dev`, `libssl-dev`, `libcurl4-openssl-dev`,
  `libcoap3-dev`, paho-mqtt C client, plus `mosquitto` / `mosquitto-clients` for MQTT.

Exact install commands are in
[`IoT-m2m/README.md`](IoT-m2m/README.md#2-install-dependencies).

---

## Documentation index

| Document | Scope |
|----------|-------|
| [`IoT-m2m/README.md`](IoT-m2m/README.md) | Full setup, build, run, database and testing guide for the CSE server |
| [`IoT-m2m/READ_ME/`](IoT-m2m/READ_ME/) | Reference notes: example API requests, VM configuration, legacy SQL schema |
| [`CoAP_Client/READ_ME.txt`](CoAP_Client/READ_ME.txt) | How to run the Python CoAP clients |

---

## Notes & known limitations

- **CSEBase startup is interactive:** running `./iotm2m` prompts for the CSEBase
  resourceName (default `mn-name`) and whether to recreate it.
- **Scheduler granularity is 1 minute:** the cron has 5 fields (no seconds/year) and
  the evaluation thread wakes every 60 s.
- **Only the `CREATE` action primitive is wired up;** other primitives are logged as
  not yet supported.
- **Schedule→Action linking is at AE level** (shared ancestry), not 1-to-1.
- **SQLite has no `busy_timeout`,** so concurrent access can occasionally log a
  benign `database is locked` from the cleanup thread (see the integration doc).
- **Committed `iotm2m.db` may be stale:** regenerate from `create_tables.sql` on a
  fresh clone so the `actions`/`schedules` tables exist.
```
