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
//   Ticket refresh scheduler (shipping plan C7). While the player is connected
//   to a server, a background thread mints a fresh game ticket from
//   POST /api/Tickets/launcher/tickets/issue every ~4 minutes (DPoP-signed,
//   bound to the launcher session) and writes it to the local ticket file the
//   game client reads (C8). The cadence stays inside the server's accepted
//   refresh window (B9: legit ~2-4 min, floor ~60s) and the 300s ticket
//   lifetime, so the session never lapses (B8).
//
//-----------------------------------------------------------------------------

#pragma once

#include <wx/string.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

class DpopKey;

class TicketRefresher
{
  public:
	// key must outlive the refresher (typically the app-owned DPoP key).
	TicketRefresher(const wxString& apiBaseUrl, const wxString& sessionToken,
	                const DpopKey& key, int serverId, const wxString& hwidJson,
	                const wxString& ticketFilePath);
	~TicketRefresher();

	TicketRefresher(const TicketRefresher&) = delete;
	TicketRefresher& operator=(const TicketRefresher&) = delete;

	// Mint one ticket synchronously (so the game has a ticket to present on its
	// initial connect), then start the background refresh loop. Returns false if
	// that first mint fails.
	bool Start();

	// Signal the loop to stop and join the thread. Idempotent; also called by
	// the destructor.
	void Stop();

	// True once at least one ticket has been written and none has since failed.
	bool HasTicket() const;

  private:
	void Loop();
	bool FetchAndWriteOnce();
	bool WriteTicketFile(const std::string& ticket);

	wxString m_apiBaseUrl;
	wxString m_sessionToken;
	const DpopKey& m_key;
	int m_serverId;
	wxString m_hwidJson;
	wxString m_ticketFilePath;

	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	bool m_stop = false;
	std::atomic<bool> m_hasTicket{false};
};
