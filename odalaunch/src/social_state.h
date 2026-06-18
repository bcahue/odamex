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
//   UI-thread-only aggregate of the player's social state: friends, pending
//   requests, online participants, blocks, and the global-chat scrollback.
//   SocialController mutates this on the UI thread (via marshalled callbacks);
//   the Friends / Chat / Players views read it and re-render on OnChanged().
//
//-----------------------------------------------------------------------------

#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

struct SocialFriend
{
	std::string subject;
	std::string username;
	std::string status;   // "Offline" | "Online" | "InMatch"
	int serverId = 0;
};

struct SocialRequest
{
	long long requestId = 0;
	std::string fromSubject;
	std::string fromUsername;
	std::string toSubject;
	std::string toUsername;
	std::string createdAt;
};

struct SocialParticipant
{
	std::string subject;
	std::string username;
	std::string status;
	int serverId = 0;
	bool isSelf = false;
};

struct SocialChatLine
{
	std::string messageId;
	std::string authorSubject;
	std::string authorUsername;
	std::string text;
	std::string sentAt;
	bool deleted = false;
};

struct SocialBlocked
{
	std::string subject;
	std::string username;    // falls back to the subject when unknown
	std::string blockedSince; // ISO-8601 UTC
};

class SocialState
{
  public:
	// Realtime hub connection state, surfaced so the chat tab can tell the user
	// when chat is unavailable (connecting / dropped) rather than failing silently.
	enum class HubState
	{
		Connecting,
		Connected,
		Disconnected
	};
	HubState hubState = HubState::Connecting;

	std::vector<SocialFriend> friends;
	std::vector<SocialRequest> incoming;
	std::vector<SocialRequest> outgoing;
	std::vector<SocialParticipant> participants;
	std::set<std::string> blocked;              // subjects only; fast IsBlocked lookups
	std::vector<SocialBlocked> blockedPlayers;  // same set, with usernames, for the Players tab
	std::vector<SocialChatLine> chat; // newest last; capped by the controller
	int slowModeSeconds = 0;

	bool IsBlocked(const std::string& sub) const { return blocked.count(sub) != 0; }

	// The views register here; the controller fires it after every mutation.
	void SetOnChanged(std::function<void()> h) { m_onChanged = std::move(h); }
	void NotifyChanged() const
	{
		if (m_onChanged)
			m_onChanged();
	}

  private:
	std::function<void()> m_onChanged;
};
