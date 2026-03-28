# Telemetry Status Report — v3.7.29

## Fix Status: RECOVERED
**Date:** 2026-03-28  
**Focus:** Telemetry Object & Live-Follow Reliability

### 1. Fix Summary (Telemetry Object)
In version 3.7.28, the telemetry object and "Live-Follow" functionality failed due to CORS issues and improper HTTP header management. 
Version 3.7.29 restores full functionality by:
- **Modularization:** Moving the telemetry HTTP handlers into `Telemetry.cpp`.
- **CORS Support:** Adding `Access-Control-Allow-Origin: *` to all critical endpoints (`/status`, `/telemetry`, `/cmd`).
- **Download Integrity:** Restoration of `Content-Disposition` for CSV file naming.
- **Client Resilience:** Fixing JavaScript syntax and adding detailed error feedback in `telemetry_viewer.html`.

### 2. Affected Components
- `v3/src/Telemetry.cpp` / `Telemetry.h`: Now contains the server handler logic.
- `v3/src/main_v3.cpp`: Simplified, only activates the telemetry module.
- `v3/telemetry_viewer.html`: Repaired "Paste" area and fetch logic.

### 3. Verification Steps
- [x] CORS headers verified on `/status` (Essential for Live-Sync).
- [x] CORS headers verified on `/telemetry` (Essential for loading data).
- [x] "Paste Area" restored for manual CSV analysis.
- [x] Firmware version bumped to **3.7.29**.

### 4. Known Status
The "lifefeed" (Live-Sync) is operational again. If the viewer is run locally (via file:// or another server), it can now correctly communicate with `perlin-v3.local`.
