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

// Upload a finished match's stats to the API (B7 / S6).
//
// statsPayloadJson is the already-serialized v6 GameV6 blob (see
// SV_SerializeMatchStats); this wraps it in the SubmitMatchStatsCommand envelope
// (serverId + match window) and spools it to disk (under the wdlstats log dir,
// stats/unsent/) before queueing it for asynchronous delivery on the same worker
// thread + service token as the event posts. The POST goes to
//   {sv_auth_api_url}/api/GameServerStats/game-servers/stats
// with `Authorization: Bearer <token>` and `Content-Type: application/json`.
// On success the spooled file moves to sent/; on a permanent failure (4xx incl.
// a 409 duplicate) to rejected/; a transient outage leaves it in unsent/ to be
// retried later — so a restart never drops an upload. Returns immediately; no-op
// when auth is disabled / the worker isn't running / the payload is empty /
// the spool is unavailable. startedAtUnix / endedAtUnix are unix seconds.
void SV_ApiUploadMatchStats(const std::string& statsPayloadJson, int64_t startedAtUnix,
                            int64_t endedAtUnix);

// Stops the worker thread (draining any in-flight queue best-effort) and
// releases resources. Registered via atterm.
void STACK_ARGS SV_ApiShutdown();
