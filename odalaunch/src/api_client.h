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
//   Authenticated REST client for the social API (friends, blocks, global-chat
//   participants/history). Synchronous: each call blocks on the HTTP round-trip,
//   so invoke from a worker thread, never the UI thread. Mirrors the
//   Bearer + DPoP request pattern in ticket_refresher.cpp.
//
//-----------------------------------------------------------------------------

#pragma once

#include <wx/string.h>

#include <functional>
#include <string>

class DpopKey;

class ApiClient
{
  public:
	struct Response
	{
		bool ok = false;   // transport completed AND HTTP 2xx
		int status = 0;    // HTTP status, 0 if the request never completed
		std::string body;  // response body (JSON for the GET endpoints)
	};

	// tokenProvider yields the current session JWT per request (it rotates as
	// the ticket refresher renews it). `key` is the DPoP key; it must outlive
	// this client (LauncherSession owns it).
	ApiClient(wxString apiBaseUrl, std::function<std::string()> tokenProvider,
	          const DpopKey& key);

	// Reads.
	Response GetFriends();
	Response GetFriendRequests(const std::string& direction); // "incoming" | "outgoing"
	Response GetBlocks();
	Response GetGlobalChatHistory();
	Response GetParticipants();

	// Mutations.
	Response SendFriendRequest(const std::string& targetSubject);
	Response AcceptFriendRequest(long long requestId);
	Response DeclineFriendRequest(long long requestId);
	Response CancelFriendRequest(long long requestId);
	Response RemoveFriend(const std::string& subject);
	Response Block(const std::string& targetSubject);
	Response Unblock(const std::string& subject);

  private:
	Response Send(const char* method, const wxString& path, const std::string& jsonBody);

	wxString m_apiBaseUrl;
	std::function<std::string()> m_tokenProvider;
	const DpopKey& m_key;
};
