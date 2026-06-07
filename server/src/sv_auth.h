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
//   Player authentication via JWT game tickets.
//   Fetches JWKS from the Odamex API and verifies ES256-signed tickets.
//
//-----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>

#include "doomtype.h"

struct TicketResult
{
	bool valid;
	std::string sub;       // Player's Keycloak subject ID
	std::string jti;       // Unique ticket ID (for replay tracking by caller)
	int srv;               // Target server ID from the ticket
	int64_t expiresAt;     // Unix timestamp when ticket expires
	std::string username;  // Player's Odamex username (preferred_username claim)
	std::string reason;    // Human-readable failure reason (empty on success)
};

// Called once at server startup after CVARs are loaded.
// If sv_auth_enabled is true, fetches the JWKS from the API.
// Fatal on failure: if auth is enabled and the fetch fails, the server
// aborts startup via I_FatalError.
void SV_AuthInit();

// Called from SV_RunTics each tick.
// Handles periodic JWKS refresh (~24h) and retry logic.
void SV_AuthTick();

// Verify a JWT game ticket. Returns a TicketResult.
// On success: valid=true, sub/jti/srv/expiresAt populated.
// On failure: valid=false, reason populated.
TicketResult SV_AuthVerifyTicket(const std::string& jwt);

// Returns true if auth is enabled AND the JWKS key has been
// successfully fetched (i.e., the server can verify tickets).
bool SV_AuthReady();

// Single-use (replay) tracking for ticket jti claims (B4).
// Registers a jti as seen. Returns true if the jti was newly registered
// (caller may accept), false if it has already been seen within its lifetime
// (replay -- caller must reject). expiresAt is the ticket's exp (unix seconds);
// the jti is retained until exp + grace, after which it is pruned. Expired
// entries are swept on each call so the set stays bounded.
bool SV_AuthRegisterJti(const std::string& jti, int64_t expiresAt);

// Cleanup. Called on server shutdown via atterm.
void STACK_ARGS SV_AuthShutdown();
