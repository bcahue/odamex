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
//   Ticket refresh scheduler. See ticket_refresher.h.
//
//-----------------------------------------------------------------------------

// Mongoose for JSON parsing (mg_json_get_str). Must precede wx headers so its
// <winsock2.h> wins the include-order race with wx's <windows.h>.
#include "mongoose.h"

#include "ticket_refresher.h"

#include <chrono>
#include <string>

#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/webrequest.h>

#include "dpop_key.h"
#include "dpop_proof.h"

namespace
{
// Refresh interval. Chosen with the rest of the refresh timing budget (C8 note):
// the game client resends every ~90s and the server kicks at exp + 60s grace
// (B8), with a 300s ticket lifetime. 120s here keeps
//   launcher(120) + 2*client(90) = 300 <= lifetime(300) + grace(60) = 360,
// so the session survives even one dropped (unreliable) client refresh. Stays
// well inside the /tickets rate limit (30/60s).
const int kRefreshIntervalSeconds = 120;

const char* const kTicketPath = "/api/Tickets/launcher/tickets/issue";
} // namespace

TicketRefresher::TicketRefresher(const wxString& apiBaseUrl,
                                 const wxString& sessionToken, const DpopKey& key,
                                 int serverId, const wxString& hwidJson,
                                 const wxString& ticketFilePath)
    : m_apiBaseUrl(apiBaseUrl), m_sessionToken(sessionToken), m_key(key),
      m_serverId(serverId), m_hwidJson(hwidJson.empty() ? wxString("{}") : hwidJson),
      m_ticketFilePath(ticketFilePath)
{
	while (m_apiBaseUrl.EndsWith("/"))
		m_apiBaseUrl.RemoveLast();
}

TicketRefresher::~TicketRefresher()
{
	Stop();
}

bool TicketRefresher::Start()
{
	// Mint the first ticket synchronously so the game has one to present on its
	// initial connect.
	if (!FetchAndWriteOnce())
		return false;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop = false;
	}
	m_thread = std::thread([this]() { Loop(); });
	return true;
}

void TicketRefresher::Stop()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop = true;
	}
	m_cv.notify_all();
	if (m_thread.joinable())
		m_thread.join();
}

bool TicketRefresher::HasTicket() const
{
	return m_hasTicket.load();
}

void TicketRefresher::Loop()
{
	for (;;)
	{
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			// Interruptible wait: wakes early if Stop() sets m_stop.
			m_cv.wait_for(lock, std::chrono::seconds(kRefreshIntervalSeconds),
			              [this]() { return m_stop; });
			if (m_stop)
				return;
		}

		// A failed refresh isn't fatal: the previous ticket is still valid for a
		// while, and we'll try again next cycle. B8 on the server is the backstop
		// if refreshes keep failing past the ticket's expiry.
		FetchAndWriteOnce();
	}
}

bool TicketRefresher::FetchAndWriteOnce()
{
	const wxString url = m_apiBaseUrl + kTicketPath;
	const std::string urlUtf8 = url.utf8_string();

	// DPoP proof bound to this request and to the session token (ath). The
	// session JWT is both the Bearer credential and the access token the proof
	// is bound to.
	const std::string sessionUtf8 = m_sessionToken.utf8_string();
	const std::string proof =
	    DpopProof::Create(m_key, "POST", urlUtf8, sessionUtf8);
	if (proof.empty())
	{
		m_hasTicket.store(false);
		return false;
	}

	std::string body = "{\"serverId\":";
	body += std::to_string(m_serverId);
	body += ",\"hwid\":";
	body += m_hwidJson.utf8_string();
	body += "}";

	wxWebSessionSync& session = wxWebSessionSync::GetDefault();
	wxWebRequestSync request = session.CreateRequest(url);
	if (!request.IsOk())
	{
		m_hasTicket.store(false);
		return false;
	}

	request.SetMethod("POST");
	request.SetData(wxString::FromUTF8(body), "application/json");
	request.SetHeader("Authorization", "Bearer " + m_sessionToken);
	request.SetHeader("DPoP", wxString::FromUTF8(proof));

	wxWebRequestSync::Result result = request.Execute();
	if (result.state != wxWebRequest::State_Completed)
	{
		m_hasTicket.store(false);
		return false;
	}

	wxWebResponse response = request.GetResponse();
	if (response.GetStatus() != 200)
	{
		// 403 = banned, 404 = server inactive, 401 = session/DPoP rejected.
		m_hasTicket.store(false);
		return false;
	}

	const std::string respBody = response.AsString().utf8_string();
	struct mg_str j = mg_str_n(respBody.data(), respBody.size());
	char* ticket = mg_json_get_str(j, "$.ticket");
	if (ticket == nullptr)
	{
		m_hasTicket.store(false);
		return false;
	}

	bool ok = WriteTicketFile(ticket);
	mg_free(ticket);

	m_hasTicket.store(ok);
	return ok;
}

bool TicketRefresher::WriteTicketFile(const std::string& ticket)
{
	// Write to a temp file then rename over the target, so the game client
	// (which polls the file) never observes a half-written ticket. The file's
	// per-user location and restrictive permissions are handled by the C8
	// handoff that chooses m_ticketFilePath.
	const wxString tmp = m_ticketFilePath + ".tmp";

	{
		wxFile file;
		if (!file.Create(tmp, /*overwrite*/ true))
			return false;
		if (!ticket.empty() &&
		    file.Write(ticket.data(), ticket.size()) != ticket.size())
		{
			file.Close();
			wxRemoveFile(tmp);
			return false;
		}
		file.Close();
	}

	if (!wxRenameFile(tmp, m_ticketFilePath, /*overwrite*/ true))
	{
		wxRemoveFile(tmp);
		return false;
	}

	return true;
}
