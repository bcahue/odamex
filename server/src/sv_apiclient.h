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
//   Asynchronous client for the Odamex API. Obtains a Keycloak service token
//   (client_credentials grant, B6) and posts player join/leave events (B5) on
//   a background worker thread so the game loop never blocks on network I/O.
//
//-----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>

#include "doomtype.h"

// Starts the background worker thread. Call once at startup after SV_AuthInit.
// No-op when sv_auth_enabled is false.
void SV_ApiInit();

// Enqueue a player join/leave event for asynchronous delivery to the API.
// eventType must be "join" or "leave". occurredAt is unix seconds.
// Returns immediately; the worker thread acquires/refreshes the service token
// and performs the POST. No-op when auth is disabled, the worker isn't running,
// or sub is empty (an unauthenticated player has no identity to report).
void SV_ApiPostPlayerEvent(const std::string& sub, const std::string& jti,
                           const std::string& eventType, int64_t occurredAt,
                           const std::string& clientIp);

// Upload a finished match's stats blob to the API (B7).
//
// SCAFFOLD: not yet implemented. The v1 stats schema is an opaque JSON blob,
// so this takes the already-serialized body produced by the sv_stats serializer
// (see SV_SerializeMatchStats). When implemented it will reuse the same service
// token + worker thread as the event posts and POST to
//   {sv_auth_api_url}/api/GameServerStats/game-servers/stats
// Currently a no-op pending the B7 design pass.
void SV_ApiUploadMatchStats(const std::string& statsJson);

// Stops the worker thread (draining any in-flight queue best-effort) and
// releases resources. Registered via atterm.
void STACK_ARGS SV_ApiShutdown();
