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
//   Launcher account-session state (shipping plan C10). Ties together the auth
//   primitives built across Phase C into one object the UI can drive:
//     - DpopKey (C3)            : the non-exportable signing key
//     - SessionStore (C5)       : persistence across restarts
//     - LauncherLogin (C2)      : the browser OIDC flow
//     - LauncherRegistration(C2b): first-time username picker
//     - Hwid (C4)               : machine fingerprint for /start and /tickets
//     - TicketRefresher (C7)    : per-connection ticket minting
//
//   This class holds *state and the non-UI orchestration helpers* only. The
//   threading (the login flow blocks while the user authenticates) and the
//   modal username picker live in dlgMain, which owns one of these.
//
//-----------------------------------------------------------------------------

#pragma once

#include <wx/string.h>

#include "dpop_key.h"

class LauncherSession
{
  public:
	LauncherSession();
	~LauncherSession();

	// API origin for all auth calls (no trailing slash). Read from config at
	// construction, defaulting to ODA_AUTHAPIURL.
	const wxString& ApiBaseUrl() const { return m_apiBaseUrl; }
	void SetApiBaseUrl(const wxString& url) { m_apiBaseUrl = url; }

	// Ensure the DPoP key exists (creating it on first run). Cheap to call
	// repeatedly; the result is cached. Returns false if the platform key
	// backend is unavailable (e.g. non-Windows stub).
	bool EnsureKey();
	bool KeyReady() const { return m_keyReady; }
	DpopKey& Key() { return m_key; }

	// Load a persisted session from the OS credential store and adopt it if the
	// token is still valid (not expired). Call once at startup, after EnsureKey.
	// Returns true if a usable session was restored (then IsSignedIn() is true).
	bool Restore();

	// Adopt a freshly obtained session (from login Success or completed
	// registration) and persist it. `username` may be empty, in which case it
	// is recovered from the token's preferred_username claim. Returns false if
	// persistence fails (state is still updated in memory).
	bool AdoptSession(const wxString& sessionToken, const wxString& username);

	// Forget the session and wipe it from the credential store.
	void SignOut();

	bool IsSignedIn() const { return m_signedIn; }
	const wxString& Username() const { return m_username; }
	const wxString& SessionToken() const { return m_token; }

	// True when the previous session ended with an explicit sign-out, so the
	// next sign-in should force a fresh Keycloak credential prompt rather than
	// silently reusing the browser's SSO session. Persisted across restarts;
	// set by SignOut(), cleared by a successful AdoptSession().
	bool ForceLogin() const { return m_forceLogin; }

	// Best-effort decode of the `preferred_username` claim from a session JWT.
	// Returns empty on any parse failure.
	static wxString UsernameFromToken(const wxString& token);

	// Best-effort decode of the `exp` (seconds since epoch) claim. Returns 0 if
	// absent/unparseable (caller should treat 0 as "unknown", not "expired").
	static int64_t ExpiryFromToken(const wxString& token);

  private:
	// Persisted blob format: {"token":"...","username":"..."}.
	wxString SerializeBlob() const;
	bool ParseBlob(const std::string& blob, wxString& token, wxString& username);

	// Persist the force-login flag to config.
	void PersistForceLogin(bool value);

	wxString m_apiBaseUrl;
	DpopKey m_key;
	bool m_keyReady;
	bool m_signedIn;
	bool m_forceLogin;
	wxString m_username;
	wxString m_token;
};
