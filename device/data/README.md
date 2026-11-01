# Road-limit pack

`limits.bin` is the speed-limit pack the firmware reads from LittleFS (`/limits.bin`). It covers
Cairo, Ismailia and the road between them, and is built from OpenStreetMap data with
`platform/apps/api/scripts/build_speed_limits.py`, which writes this pack and the server's SQLite
index from the same input. Roads without a `maxspeed` tag get a default by road class and are
flagged as estimated.

Format: see the docstring at the top of `build_speed_limits.py` ("NZSL" version 1).

Licence: derived from OpenStreetMap data, (c) OpenStreetMap contributors, available under the Open
Database License (ODbL) 1.0. This file is distributed under the ODbL, not the MIT licence. See
`../../NOTICE`.

Upload it to the board with `pio run -e nazer_esp32 -t uploadfs`.

The synthetic fixtures in `test/host/` are separate and contain no OpenStreetMap data.
