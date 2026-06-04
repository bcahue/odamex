// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 2006-2026 by The Odamex Team.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//   Hardware-identifier (HWID) collection for moderation / ban enforcement
//   (shipping plan C4). Reads a small, stable set of machine fingerprints,
//   hashes each with SHA-256 (so raw, PII-shaped values never leave the
//   machine), and packages them in the API's versioned payload schema.
//
//   The payload is submitted to the API at /api/launcher/auth/start and on
//   every /api/Tickets/launcher/tickets/issue. Collection lives entirely in the
//   launcher; the game client and servers never touch HWID.
//
//-----------------------------------------------------------------------------

#pragma once

#include <string>

namespace Hwid
{
// Collect the machine's HWID payload as the JSON object the API expects:
//   {"hwid_v":1,"components":{
//      "cpu_id":"<sha256hex>","disk_serial":"<sha256hex>",
//      "mac_hash":"<sha256hex>","machine_guid":"<sha256hex>"}}
// Each value is sha256(raw_component) in lowercase hex. Components that can't be
// read on this machine are omitted; the API tolerates a partial set (matching is
// partial), but at least one component must be present for login to succeed.
std::string CollectPayloadJson();
} // namespace Hwid
