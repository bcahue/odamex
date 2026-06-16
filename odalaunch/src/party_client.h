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

// ---- global chat / friends / presence (all on the same hub) ----

struct GlobalChatMessage
{
	std::string messageId;     // GUID; the moderation handle
	std::string authorSubject;
	std::string authorUsername;
	std::string text;
	std::string sentAt;        // ISO-8601
};

struct FriendRequestEvent
{
	long long requestId = 0;
	std::string fromSubject;
	std::string fromUsername;
	std::string createdAt;     // ISO-8601
};

struct FriendPresence
{
	std::string subject;
	std::string status;        // "Offline" | "Online" | "InMatch"
	int serverId = 0;          // 0 when not in a match
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

	// Global chat events.
	void SetOnGlobalMessage(std::function<void(const GlobalChatMessage&)> h);
	void SetOnGlobalMessageDeleted(std::function<void(const std::string& messageId)> h);
	void SetOnGlobalChatState(std::function<void(int slowModeSeconds)> h);

	// Friend-graph events.
	void SetOnFriendRequest(std::function<void(const FriendRequestEvent&)> h);
	void SetOnFriendAdded(std::function<void(const std::string& subject)> h);
	void SetOnFriendRemoved(std::function<void(const std::string& subject)> h);
	// requestId + cancelled flag (true = withdrawn by sender, false = declined by recipient).
	void SetOnFriendRequestResolved(std::function<void(long long requestId, bool cancelled)> h);
	void SetOnFriendPresenceChanged(std::function<void(const FriendPresence&)> h);

	// Client->server hub methods.
	void Invite(const std::string& inviteeSubject, InviteResultHandler cb);
	void AcceptInvite(const std::string& inviteId, ResultHandler cb);
	void DeclineInvite(const std::string& inviteId, ResultHandler cb);
	void Leave(ResultHandler cb);
	void SendMessage(const std::string& text, ResultHandler cb);
	void Kick(const std::string& targetSubject, ResultHandler cb);
	void PromoteLeader(const std::string& newLeaderSubject, ResultHandler cb);

	// Global chat send (hub-only). Friend ops go over REST (ApiClient), not the hub.
	void SendGlobalMessage(const std::string& text, ResultHandler cb);

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

	std::function<void(const GlobalChatMessage&)> m_onGlobalMessage;
	std::function<void(const std::string&)> m_onGlobalMessageDeleted;
	std::function<void(int)> m_onGlobalChatState;
	std::function<void(const FriendRequestEvent&)> m_onFriendRequest;
	std::function<void(const std::string&)> m_onFriendAdded;
	std::function<void(const std::string&)> m_onFriendRemoved;
	std::function<void(long long, bool)> m_onFriendRequestResolved;
	std::function<void(const FriendPresence&)> m_onFriendPresenceChanged;
};
