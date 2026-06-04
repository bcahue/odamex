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
//   Persistent storage for the launcher session token (shipping plan C5). The
//   session JWT is long-lived (~30 days) and DPoP-bound, so persisting it lets
//   the player stay signed in across launcher restarts without re-running the
//   browser OIDC flow. A stolen token at rest is of limited use on its own
//   because it is bound (cnf/jkt) to the non-exportable DPoP key (C3).
//
//   Windows: a CRED_TYPE_GENERIC entry in Credential Manager (DPAPI-protected
//   per user). macOS/Linux backends are stubbed for a later wave (Keychain /
//   libsecret), mirroring DpopKey.
//
//-----------------------------------------------------------------------------

#pragma once

#include <string>

namespace SessionStore
{
// Store or overwrite the session token. Returns false on failure (including a
// token larger than the platform credential store allows).
bool Save(const std::string& token);

// Load the stored token into `token`. Returns false if none is stored or on
// failure.
bool Load(std::string& token);

// Remove the stored token (sign-out). Returns true if it was removed or was
// already absent; false only on an unexpected failure.
bool Clear();
} // namespace SessionStore
