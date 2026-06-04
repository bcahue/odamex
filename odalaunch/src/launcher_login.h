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
//   Drives the launcher's authorization-code-with-PKCE login flow against the
//   Odamex API (shipping plan C2):
//     1. POST /api/launcher/auth/start  (registers DPoP key + HWID + callback)
//     2. open the returned authorize URL in the system browser
//     3. wait on the loopback listener for the API's bounce-back, which carries
//        a session token, a pending-registration token, or an error.
//
//-----------------------------------------------------------------------------

#pragma once

#include <atomic>

#include <wx/string.h>

// Outcome of a login attempt.
struct LauncherLoginResult
{
	enum class Status
	{
		Success,             // got a launcher session token
		PendingRegistration, // first-time user; must pick a username (C10)
		Error,               // API/Keycloak rejected (e.g. banned account)
		Failed               // local failure: bind, network, timeout, bad reply
	};

	Status status = Status::Failed;
	wxString sessionToken; // Status::Success
	wxString pendingToken; // Status::PendingRegistration
	wxString error;        // Status::Error / Failed: machine code or message
	wxString reference;    // Status::Error: support reference id (e.g. ban ref)
};

// Orchestrates a single login attempt. Construct, optionally supply the DPoP
// JWK / HWID payloads, then call Run() (on a worker thread -- it blocks while
// the user authenticates in the browser).
class LauncherLogin
{
  public:
	// apiBaseUrl is the API origin, e.g. "https://api.odamex.org" (no trailing
	// slash required).
	explicit LauncherLogin(const wxString& apiBaseUrl);

	// The DPoP public key as a JWK *object* (JSON), produced by C3. Until C3
	// lands this defaults to an empty object "{}"; the API will reject that at
	// runtime, but the flow and wiring are exercised.
	void SetDpopPublicJwk(const wxString& jwkJson);

	// The HWID payload as a JSON *object*, produced by C4. Defaults to "{}".
	void SetHwidPayload(const wxString& hwidJson);

	// When true, ask the API to force a fresh Keycloak credential prompt
	// (prompt=login) instead of silently reusing the browser's SSO session.
	// Set after an explicit sign-out. Defaults to false.
	void SetForceLogin(bool force) { m_forceLogin = force; }

	// Optional cancellation flag, owned by the caller. When set from another
	// thread it aborts the wait for the browser callback so Run() returns
	// promptly (status Failed, error "cancelled"). Used to unblock launcher
	// shutdown while a login is in flight. The pointer must outlive Run().
	void SetCancelFlag(std::atomic<bool>* cancel) { m_cancel = cancel; }

	// Runs the full flow synchronously. timeoutSeconds bounds how long we wait
	// for the user to finish in the browser.
	LauncherLoginResult Run(int timeoutSeconds = 300);

  private:
	wxString m_apiBaseUrl;
	wxString m_dpopJwkJson;
	wxString m_hwidJson;
	bool m_forceLogin = false;
	std::atomic<bool>* m_cancel = nullptr;
};
