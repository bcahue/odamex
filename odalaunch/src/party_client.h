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
//   Typed party/chat client (shipping plan C11). Wraps the generic
//   SignalRClient transport with the PartyHub contract: the seven hub methods
//   (Invite / AcceptInvite / DeclineInvite / Leave / SendMessage / Kick /
//   PromoteLeader) and parsed callbacks for the eight server->client events.
//
//   All event/completion callbacks fire on the transport's worker thread; the
//   caller must marshal to the UI thread (wx CallAfter).
//
//-----------------------------------------------------------------------------

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

class DpopKey;
class SignalRClient;

// ---- Parsed event payloads (camelCase JSON from the hub) ----

struct PartyInvite
{
	std::string inviteId;
	std::string partyId;
	std::string inviterSubject;
	std::string expiresAt; // ISO-8601
};

struct PartySnapshot
{
	std::string partyId;
	std::string leaderSubject;
	std::vector<std::string> memberSubjects;
	std::string createdAt;
};

struct PartyChatMessage
{
	std::string partyId;
	std::string fromSubject;
	std::string text;
	std::string sentAt;
};

struct PartyInviteDeclined
{
	std::string inviteId;
	std::string partyId;
};

struct MatchAssignment
{
	std::string mode;
	int serverId = 0;
	std::string ticket;
	std::string jti;
	std::string expiresAt;
};

struct SessionEvicted
{
	int evictedServerId = 0;
	int newServerId = 0;
	std::string evictedTicketJti;
};

// Typed front end over the PartyHub SignalR connection.
class PartyClient
{
  public:
	// ok=false carries the hub error string (e.g. "party.invite.failed:PartyFull")
	// or a transport reason ("disconnected").
	using ResultHandler = std::function<void(bool ok, const std::string& error)>;
	// On success, inviteId holds the new invite id; on failure, the error string.
	using InviteResultHandler =
	    std::function<void(bool ok, const std::string& inviteIdOrError)>;

	PartyClient(std::string apiBaseUrl,
	            std::function<std::string()> tokenProvider, const DpopKey& key);
	~PartyClient();

	PartyClient(const PartyClient&) = delete;
	PartyClient& operator=(const PartyClient&) = delete;

	// Connection lifecycle.
	void Start();
	void Stop();
	bool IsConnected() const;

	void SetOnConnected(std::function<void()> h);
	void SetOnClosed(std::function<void(const std::string& reason, bool willRetry)> h);

	// Server->client events.
	void SetOnInvited(std::function<void(const PartyInvite&)> h);
	void SetOnPartyUpdated(std::function<void(const PartySnapshot&)> h);
	void SetOnMessage(std::function<void(const PartyChatMessage&)> h);
	void SetOnDisbanded(std::function<void(const std::string& partyId)> h);
	void SetOnKicked(std::function<void(const std::string& partyId)> h);
	void SetOnInviteDeclined(std::function<void(const PartyInviteDeclined&)> h);
	void SetOnMatchAssigned(std::function<void(const MatchAssignment&)> h);
	void SetOnSessionEvicted(std::function<void(const SessionEvicted&)> h);

	// Client->server hub methods.
	void Invite(const std::string& inviteeSubject, InviteResultHandler cb);
	void AcceptInvite(const std::string& inviteId, ResultHandler cb);
	void DeclineInvite(const std::string& inviteId, ResultHandler cb);
	void Leave(ResultHandler cb);
	void SendMessage(const std::string& text, ResultHandler cb);
	void Kick(const std::string& targetSubject, ResultHandler cb);
	void PromoteLeader(const std::string& newLeaderSubject, ResultHandler cb);

  private:
	void Dispatch(const std::string& target, const std::string& argsJson);

	std::unique_ptr<SignalRClient> m_signalr;

	std::function<void(const PartyInvite&)> m_onInvited;
	std::function<void(const PartySnapshot&)> m_onPartyUpdated;
	std::function<void(const PartyChatMessage&)> m_onMessage;
	std::function<void(const std::string&)> m_onDisbanded;
	std::function<void(const std::string&)> m_onKicked;
	std::function<void(const PartyInviteDeclined&)> m_onInviteDeclined;
	std::function<void(const MatchAssignment&)> m_onMatchAssigned;
	std::function<void(const SessionEvicted&)> m_onSessionEvicted;
};
