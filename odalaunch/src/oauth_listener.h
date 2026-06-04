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
//   One-shot loopback HTTP listener for the OIDC authorization-code callback
//   (shipping plan C1). Binds 127.0.0.1 on an OS-chosen ephemeral port, serves
//   a single GET to /auth, captures the callback's query parameters, shows a
//   "you can close this window" page, and stops.
//
//-----------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <string>

// Loopback listener for the system-browser OIDC redirect. Owns a background
// thread running a minimal HTTP server bound to 127.0.0.1. The httplib
// dependency is hidden behind a pImpl so the rest of the launcher (and its wx
// precompiled header) never has to include the large single-header library.
class OAuthLoopbackListener
{
  public:
	OAuthLoopbackListener();
	~OAuthLoopbackListener();

	OAuthLoopbackListener(const OAuthLoopbackListener&) = delete;
	OAuthLoopbackListener& operator=(const OAuthLoopbackListener&) = delete;

	// Bind 127.0.0.1 on a random free port and begin listening on a background
	// thread. Returns false if the bind/listen fails.
	bool Start();

	// The bound port, or 0 when not started.
	int Port() const;

	// The redirect URI to register with the authorize request, e.g.
	// "http://127.0.0.1:49217/auth". Empty when not started.
	std::string RedirectUri() const;

	// Block until the callback arrives or timeoutSeconds elapses. On success,
	// fills params with the callback query string (e.g. "code", "state",
	// "error") and returns true. Returns false on timeout or cancellation.
	// If `cancel` is non-null it is polled while waiting; setting it from another
	// thread wakes the wait within ~200ms and makes it return false -- used so
	// the launcher can abort a pending login at shutdown instead of blocking on
	// the full timeout.
	bool WaitForCallback(int timeoutSeconds,
	                     std::map<std::string, std::string>& params,
	                     std::atomic<bool>* cancel = nullptr);

	// Stop the listener and join the worker thread. Idempotent.
	void Stop();

  private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
