# IoT-m2m — oneM2M CSE Server

oneM2M CSE server written in C with HTTP, CoAP, and MQTT support.
Implements the following resource types: CSEBase, AE, Container, ContentInstance, Subscription, and Schedule.

---

## Table of contents

1. [System requirements](#1-system-requirements)
2. [Install dependencies](#2-install-dependencies)
3. [Build](#3-build)
4. [Initialise the database](#4-initialise-the-database)
5. [Run the server](#5-run-the-server)
6. [Test](#6-test)
7. [Stop the server](#7-stop-the-server)
8. [Debug and memory](#8-debug-and-memory)

---

## 1. System requirements

- Ubuntu 22.04 / 24.04 (or WSL2 with Ubuntu 24.04)
- GCC 13+
- CMake 3.16+ (to build libcoap and paho-mqtt from source)

---

## 2. Install dependencies

Run these commands **in order**.

### 2.1 Base toolchain
```bash
sudo apt update
sudo apt install -y build-essential cmake git gdb valgrind
```

### 2.2 Libraries available via apt
```bash
sudo apt install -y \
    libsqlite3-dev \
    libjson-c-dev \
    libssl-dev \
    libcurl4-openssl-dev \
    mosquitto \
    mosquitto-clients
```

### 2.3 libcoap-3

**Option A — apt (Ubuntu 22.04/24.04, recommended):**
```bash
sudo apt install -y libcoap3-dev libcoap3
# Ubuntu 24.04 installs the OpenSSL variant: libcoap-3-openssl
# The makefile already uses -lcoap-3-openssl for compatibility.
```

**Option B — build from source (if apt unavailable):**
```bash
git clone https://github.com/obgm/libcoap.git
cd libcoap
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_TESTS=OFF \
      -DENABLE_EXAMPLES=OFF \
      -DENABLE_DOCUMENTATION=OFF ..
make -j$(nproc)
sudo make install
sudo ldconfig
cd ../..
# Verify:
pkg-config --modversion libcoap-3
```

### 2.4 paho-mqtt C client

**Option A — apt (Ubuntu 24.04+):**
```bash
sudo apt install -y libpaho-mqtt-dev
```

**Option B — build from source (if apt fails):**
```bash
git clone https://github.com/eclipse/paho.mqtt.c.git
cd paho.mqtt.c
cmake -Bbuild \
      -DPAHO_WITH_SSL=ON \
      -DPAHO_BUILD_STATIC=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build --target install -j$(nproc)
sudo ldconfig
cd ..
```

### 2.5 Verify all libraries
```bash
pkg-config --modversion libcoap-3 json-c sqlite3 libssl libcurl
ls /usr/local/lib/libpaho* /usr/lib/libpaho* 2>/dev/null
```

---

## 3. Build

```bash
# Enter the server directory
cd IoT-m2m

# Normal build (no logs)
make

# Build with logging enabled (recommended for development)
make ENABLE_LOGGING=1

# Clean build artifacts
make clean
```

> **WSL2 note:** if `gcc` is not found, install it with `sudo apt install build-essential`.

### Available makefile flags

| Flag | Effect |
|------|--------|
| `ENABLE_LOGGING=1` | Enables LOG/LOG_ERROR macros on stderr |
| `-DENABLE_HTTP` | On by default — HTTP server on port 8080 |
| `-DENABLE_COAP` | On by default — CoAP server on port 5683 |
| `-DENABLE_MQTT` | On by default — MQTT client |

To build **without** a module (e.g. without MQTT while developing):
```bash
# Temporarily comment out the relevant line in the makefile:
# CFLAGS += -DENABLE_MQTT
```

---

## 4. Initialise the database

The server creates all tables automatically on startup.
To inspect or reset manually:

```bash
# Create/reset all tables
sqlite3 database/iotm2m.db < database/create_tables.sql

# Inspect schema
sqlite3 database/iotm2m.db ".schema"

# Delete all data (keep structure)
sqlite3 database/iotm2m.db "
  DELETE FROM schedules;
  DELETE FROM content_instances;
  DELETE FROM containers;
  DELETE FROM application_entities;
  DELETE FROM cse_bases;
"
```

---

## 5. Run the server

```bash
cd IoT-m2m
./iotm2m
```

On startup the server:
1. Creates/verifies all tables (including `schedules`)
2. Starts the HTTP thread (port **8080**)
3. Starts the CoAP thread (port **5683**)
4. Starts the expired-resource cleanup thread
5. Starts the **schedule evaluation thread** (runs every 60 s)

Expected output with `ENABLE_LOGGING=1`:
```
[LOG] [DB] 'schedules' table verified/initialized successfully.
[LOG] [HTTP] Server listening on port 8080...
[LOG] [CoAP] Server listening on port 5683...
[LOG] [Scheduler] Thread created.
[LOG] [Scheduler] Background thread started.
```

### Ports used

| Protocol | Port | Compile flag |
|----------|------|--------------|
| HTTP     | 8080 | `ENABLE_HTTP` |
| CoAP/UDP | 5683 | `ENABLE_COAP` |
| MQTT     | 1883 | `ENABLE_MQTT` (external broker) |

---

## 6. Test

### 6.1 Manual tests with curl

```bash
HOST=127.0.0.1:8080
CSE=mn-name

# Create AE
curl -s -X POST http://$HOST/$CSE \
  -H "Content-Type: application/json" \
  -d '{"m2m:ae":{"rn":"sensorAE","api":"N01.com.test","rr":true,"srv":["3"],"poa":["http://127.0.0.1:8080"]}}'

# Create Schedule
curl -s -X POST http://$HOST/$CSE/sensorAE \
  -H "Content-Type: application/json" \
  -d '{"m2m:sch":{"rn":"mySched","sce":"30 9 * * 1-5"}}'

# GET Schedule
curl -s http://$HOST/$CSE/sensorAE/mySched?ty=29

# PUT Schedule (updates lt)
curl -s -X PUT "http://$HOST/$CSE/sensorAE/mySched?ty=29" \
  -H "Content-Type: application/json" \
  -d '{"m2m:sch":{}}'

# DELETE Schedule
curl -s -X DELETE "http://$HOST/$CSE/sensorAE/mySched?ty=29"
```

### 6.2 Automated test suite (Schedule)

```bash
# Install requests if needed
pip3 install requests

# Run tests (server must be running)
python3 CoAP_Client/test_schedule.py

# Server on another host
python3 CoAP_Client/test_schedule.py 192.168.1.30:8080
```

Scenarios covered: POST 201, duplicate POST 403, GET 200/404, PUT 200, DELETE 200/404.

### 6.3 CoAP tests (Python aiocoap)

```bash
pip3 install aiocoap

python3 CoAP_Client/client_test_AE.py
python3 CoAP_Client/client_test_Container.py
python3 CoAP_Client/client_test_ContentInstance.py
```

> Edit `CoAP_Client/define_ip.py` to point to the correct IP before running.

### 6.4 Testing the scheduler thread

Create a schedule that fires **in the current minute**:
```bash
MIN=$(date +%-M)
curl -s -X POST http://127.0.0.1:8080/mn-name/sensorAE \
  -H "Content-Type: application/json" \
  -d "{\"m2m:sch\":{\"rn\":\"nowSched\",\"sce\":\"$MIN * * * *\"}}"
```
Wait up to 60 s. The log will show:
```
[LOG] [Scheduler] Schedule matched ('42 * * * *') — triggering actions.
```

---

## 7. Stop the server

```bash
Ctrl+C
```

The server handles `SIGINT`/`SIGTERM`:
1. Stops accepting new connections
2. Signals all threads to exit
3. Waits for the scheduler thread to join cleanly
4. Exits

Expected output:
```
[LOG] [Scheduler] Thread joined cleanly.
[HTTP] Server stopped.
```

---

## 8. Debug and memory

```bash
# Run with valgrind (leak detection)
valgrind --leak-check=full --show-leak-kinds=all ./iotm2m

# Run with valgrind + logging
make ENABLE_LOGGING=1 && valgrind --leak-check=full ./iotm2m
```

### Project structure

```
IoT-m2m/
├── main.c / main.h          # Startup, HTTP/CoAP routing, expiry thread
├── makefile
├── database/
│   ├── create_tables.sql    # SQL schema
│   └── iotm2m.db            # SQLite database (generated at runtime)
├── handlers/
│   ├── ae.c                 # Application Entity
│   ├── cseBase.c            # CSEBase
│   ├── container.c          # Container
│   ├── contentInstance.c    # ContentInstance
│   ├── subscription.c       # Subscription + MQTT/HTTP notifications
│   ├── schedule.c           # Schedule CRUD + background scheduler thread
│   ├── mqtt_client.c        # MQTT client (paho)
│   ├── mqtt_handler.c       # MQTT message handler
│   └── common.c             # Shared utilities
└── include/
    ├── schedule.h           # Public scheduler API
    ├── logger.h             # LOG / LOG_ERROR macros
    └── ...
```
