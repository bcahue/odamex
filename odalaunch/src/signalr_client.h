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
//   Minimal SignalR (JSON hub protocol) client over a WinHTTP WebSocket
//   (shipping plan C11). No cpprestsdk, no Microsoft SignalR-Client-Cpp: the
//   JSON hub protocol over WebSockets is small enough to speak directly, and
//   WinHTTP gives us Schannel TLS validated against the Windows cert store for
//   the wss leg to Azure SignalR Service.
//
//   Designed for the Azure SignalR "Default" topology: the /negotiate POST is
//   the only request that touches the API's HTTP pipeline (so it carries the
//   Bearer + DPoP proof, satisfying DPoPMiddleware), and the negotiate response
//   redirects the actual WebSocket to *.service.signalr.net with a service
//   token. Direct (Kestrel, no Azure) negotiate is also handled as a fallback.
//
//   Transport only: it knows nothing about party/chat semantics. A typed
//   wrapper (party_client) layers the hub method/event contract on top.
//
//-----------------------------------------------------------------------------

#pragma once

#include <functional>
#include <memory>
#include <string>

class DpopKey;

// One SignalR hub connection. Owns a background lifecycle thread that
// negotiates, opens the WebSocket, performs the JSON handshake, then pumps
// incoming records and reconnects on drop until Stop(). The WinHTTP dependency
// is hidden behind a pImpl so the rest of the launcher never includes
// <winhttp.h> (which fights wx's <windows.h>).
//
// Threading: all handler callbacks fire on the internal worker thread. The
// caller is responsible for marshalling to the UI thread (e.g. wx CallAfter).
// Send()/Invoke() are safe to call from any thread.
class SignalRClient
{
  public:
	// target = hub event name (e.g. "PartyUpdated"); argsJson = the raw JSON
	// array of arguments as sent by the server (e.g. "[{...}]").
	using EventHandler = std::function<void(const std::string& target,
	                                        const std::string& argsJson)>;

	// Fired after a successful handshake (initial connect and each reconnect).
	using ConnectedHandler = std::function<void()>;

	// Fired when the connection drops or is closed. `willRetry` is true when the
	// client intends to reconnect on its own; false after Stop() or a fatal
	// (non-retryable) close.
	using ClosedHandler = std::function<void(const std::string& reason,
	                                         bool willRetry)>;

	// Completion of an Invoke() that expected a result. `ok` false means the hub
	// threw (HubException); `payload` is the error string, otherwise the raw
	// JSON result value (may be empty for void methods).
	using CompletionHandler = std::function<void(bool ok,
	                                             const std::string& payload)>;

	// tokenProvider yields the current launcher session JWT each time it is
	// needed (it may rotate across reconnects as the ticket refresher renews
	// the session). `key` is the DPoP key whose thumbprint matches the token's
	// cnf.jkt; it must outlive this client (LauncherSession owns it).
	SignalRClient(std::string apiBaseUrl, std::string hubPath,
	              std::function<std::string()> tokenProvider, const DpopKey& key);
	~SignalRClient();

	SignalRClient(const SignalRClient&) = delete;
	SignalRClient& operator=(const SignalRClient&) = delete;

	void SetOnConnected(ConnectedHandler h);
	void SetOnClosed(ClosedHandler h);
	void SetOnEvent(EventHandler h);

	// Begin connecting on a background thread. Idempotent; a second call while
	// already running is a no-op.
	void Start();

	// Gracefully close the WebSocket and join all worker threads. Idempotent.
	void Stop();

	// Non-blocking variant for application exit: signals stop and best-effort
	// closes the socket, then DETACHES the worker threads instead of joining, so
	// a blocking connect/receive can't stall exit. The owner must be leaked
	// afterwards (the detached threads reference the impl until the process exits).
	void Abandon();

	bool IsConnected() const;

	// Fire-and-forget hub invocation (no result tracked). `argsJson` must be a
	// JSON array literal, e.g. R"(["subject-123"])" or "[]". Dropped (and logged
	// to the close handler is NOT triggered) if currently disconnected.
	void Send(const std::string& target, const std::string& argsJson);

	// Hub invocation expecting a completion. `cb` fires on the worker thread
	// when the server's completion record arrives. If disconnected, `cb` is
	// invoked immediately with ok=false.
	void Invoke(const std::string& target, const std::string& argsJson,
	            CompletionHandler cb);

  private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
