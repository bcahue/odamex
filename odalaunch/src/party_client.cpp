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
//   Typed PartyHub client over SignalRClient. See party_client.h.
//
//-----------------------------------------------------------------------------

#include "mongoose.h"

#include "party_client.h"

#include <string>

#include "json_util.h"
#include "signalr_client.h"

namespace
{
const char* const kPartyHubPath = "/hubs/party";

// JSON string literal (with surrounding quotes), escaping the characters that
// JSON requires. Hub method arguments are sent as a JSON array, and chat text
// is arbitrary user input, so this must be correct.
std::string JsonString(const std::string& s)
{
	std::string out = "\"";
	for (char c : s)
	{
		switch (c)
		{
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (static_cast<unsigned char>(c) < 0x20)
			{
				char buf[8];
				mg_snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)(unsigned char)c);
				out += buf;
			}
			else
			{
				out += c;
			}
		}
	}
	out += "\"";
	return out;
}

// A single-string argument array, e.g. ["subject-123"].
std::string Args1(const std::string& a)
{
	return "[" + JsonString(a) + "]";
}

std::string GetStr(const struct mg_str& j, const char* path)
{
	// JsonGetStr (not mg_json_get_str) so non-ASCII text -- chat messages,
	// usernames -- survives: Mongoose truncates at the first \uXXXX > 0x7F.
	return JsonGetStr(j, path);
}

int GetInt(const struct mg_str& j, const char* path)
{
	return (int)mg_json_get_long(j, path, 0);
}

long long GetLong(const struct mg_str& j, const char* path)
{
	return (long long)mg_json_get_long(j, path, 0);
}
} // namespace

PartyClient::PartyClient(std::string apiBaseUrl,
                         std::function<std::string()> tokenProvider,
                         const DpopKey& key)
    : m_signalr(new SignalRClient(std::move(apiBaseUrl), kPartyHubPath,
                                  std::move(tokenProvider), key))
{
	m_signalr->SetOnEvent(
	    [this](const std::string& target, const std::string& args) {
		    Dispatch(target, args);
	    });
}

PartyClient::~PartyClient() = default;

void PartyClient::Start()
{
	m_signalr->Start();
}

void PartyClient::Stop()
{
	m_signalr->Stop();
}

bool PartyClient::IsConnected() const
{
	return m_signalr->IsConnected();
}

void PartyClient::SetOnConnected(std::function<void()> h)
{
	m_signalr->SetOnConnected(std::move(h));
}

void PartyClient::SetOnClosed(
    std::function<void(const std::string&, bool)> h)
{
	m_signalr->SetOnClosed(std::move(h));
}

void PartyClient::SetOnInvited(std::function<void(const PartyInvite&)> h)
{
	m_onInvited = std::move(h);
}
void PartyClient::SetOnPartyUpdated(std::function<void(const PartySnapshot&)> h)
{
	m_onPartyUpdated = std::move(h);
}
void PartyClient::SetOnMessage(std::function<void(const PartyChatMessage&)> h)
{
	m_onMessage = std::move(h);
}
void PartyClient::SetOnDisbanded(std::function<void(const std::string&)> h)
{
	m_onDisbanded = std::move(h);
}
void PartyClient::SetOnKicked(std::function<void(const std::string&)> h)
{
	m_onKicked = std::move(h);
}
void PartyClient::SetOnInviteDeclined(
    std::function<void(const PartyInviteDeclined&)> h)
{
	m_onInviteDeclined = std::move(h);
}
void PartyClient::SetOnMatchAssigned(std::function<void(const MatchAssignment&)> h)
{
	m_onMatchAssigned = std::move(h);
}
void PartyClient::SetOnSessionEvicted(std::function<void(const SessionEvicted&)> h)
{
	m_onSessionEvicted = std::move(h);
}
void PartyClient::SetOnGlobalMessage(std::function<void(const GlobalChatMessage&)> h)
{
	m_onGlobalMessage = std::move(h);
}
void PartyClient::SetOnGlobalMessageDeleted(std::function<void(const std::string&)> h)
{
	m_onGlobalMessageDeleted = std::move(h);
}
void PartyClient::SetOnGlobalChatState(std::function<void(int)> h)
{
	m_onGlobalChatState = std::move(h);
}
void PartyClient::SetOnGlobalParticipantsChanged(std::function<void()> h)
{
	m_onGlobalParticipantsChanged = std::move(h);
}
void PartyClient::SetOnFriendRequest(std::function<void(const FriendRequestEvent&)> h)
{
	m_onFriendRequest = std::move(h);
}
void PartyClient::SetOnFriendAdded(std::function<void(const std::string&)> h)
{
	m_onFriendAdded = std::move(h);
}
void PartyClient::SetOnFriendRemoved(std::function<void(const std::string&)> h)
{
	m_onFriendRemoved = std::move(h);
}
void PartyClient::SetOnFriendRequestResolved(std::function<void(long long, bool)> h)
{
	m_onFriendRequestResolved = std::move(h);
}
void PartyClient::SetOnFriendPresenceChanged(std::function<void(const FriendPresence&)> h)
{
	m_onFriendPresenceChanged = std::move(h);
}

// ---- hub methods ----
//
// Method/event names mirror PartyHub.cs exactly. Void hub methods report only
// success/failure; Invite returns the new invite id as its result.

void PartyClient::Invite(const std::string& inviteeSubject, InviteResultHandler cb)
{
	m_signalr->Invoke("Invite", Args1(inviteeSubject),
	                  [cb](bool ok, const std::string& payload) {
		                  if (!cb)
			                  return;
		                  if (!ok)
		                  {
			                  cb(false, payload);
			                  return;
		                  }
		                  // payload is a bare JSON string, e.g. "invite-123".
		                  struct mg_str j = mg_str_n(payload.data(), payload.size());
		                  std::string id = GetStr(j, "$");
		                  cb(true, id);
	                  });
}

void PartyClient::AcceptInvite(const std::string& inviteId, ResultHandler cb)
{
	m_signalr->Invoke("AcceptInvite", Args1(inviteId), std::move(cb));
}

void PartyClient::DeclineInvite(const std::string& inviteId, ResultHandler cb)
{
	m_signalr->Invoke("DeclineInvite", Args1(inviteId), std::move(cb));
}

void PartyClient::Leave(ResultHandler cb)
{
	m_signalr->Invoke("Leave", "[]", std::move(cb));
}

void PartyClient::SendMessage(const std::string& text, ResultHandler cb)
{
	m_signalr->Invoke("SendMessage", Args1(text), std::move(cb));
}

void PartyClient::Kick(const std::string& targetSubject, ResultHandler cb)
{
	m_signalr->Invoke("Kick", Args1(targetSubject), std::move(cb));
}

void PartyClient::PromoteLeader(const std::string& newLeaderSubject, ResultHandler cb)
{
	m_signalr->Invoke("PromoteLeader", Args1(newLeaderSubject), std::move(cb));
}

void PartyClient::SendGlobalMessage(const std::string& text, ResultHandler cb)
{
	m_signalr->Invoke("SendGlobalMessage", Args1(text), std::move(cb));
}

// ---- event dispatch ----
//
// args is the raw JSON arguments array; every PartyHub event carries a single
// object, so the payload lives at $[0].

void PartyClient::Dispatch(const std::string& target, const std::string& args)
{
	struct mg_str j = mg_str_n(args.data(), args.size());

	if (target == "PartyInvited")
	{
		if (!m_onInvited)
			return;
		PartyInvite inv;
		inv.inviteId = GetStr(j, "$[0].inviteId");
		inv.partyId = GetStr(j, "$[0].partyId");
		inv.inviterSubject = GetStr(j, "$[0].inviterSubject");
		inv.expiresAt = GetStr(j, "$[0].expiresAt");
		m_onInvited(inv);
	}
	else if (target == "PartyUpdated")
	{
		if (!m_onPartyUpdated)
			return;
		PartySnapshot snap;
		snap.partyId = GetStr(j, "$[0].partyId");
		snap.leaderSubject = GetStr(j, "$[0].leaderSubject");
		snap.createdAt = GetStr(j, "$[0].createdAt");
		// memberSubjects is a JSON array; walk indices until one is absent.
		for (int i = 0;; ++i)
		{
			char path[48];
			mg_snprintf(path, sizeof(path), "$[0].memberSubjects[%d]", i);
			char* v = mg_json_get_str(j, path);
			if (!v)
				break;
			snap.memberSubjects.emplace_back(v);
			mg_free(v);
		}
		m_onPartyUpdated(snap);
	}
	else if (target == "PartyMessageReceived")
	{
		if (!m_onMessage)
			return;
		PartyChatMessage msg;
		msg.partyId = GetStr(j, "$[0].partyId");
		msg.fromSubject = GetStr(j, "$[0].message.fromSubject");
		msg.text = GetStr(j, "$[0].message.text");
		msg.sentAt = GetStr(j, "$[0].message.sentAt");
		m_onMessage(msg);
	}
	else if (target == "PartyDisbanded")
	{
		if (m_onDisbanded)
			m_onDisbanded(GetStr(j, "$[0].partyId"));
	}
	else if (target == "PartyKicked")
	{
		if (m_onKicked)
			m_onKicked(GetStr(j, "$[0].partyId"));
	}
	else if (target == "PartyInviteDeclined")
	{
		if (!m_onInviteDeclined)
			return;
		PartyInviteDeclined d;
		d.inviteId = GetStr(j, "$[0].inviteId");
		d.partyId = GetStr(j, "$[0].partyId");
		m_onInviteDeclined(d);
	}
	else if (target == "MatchmakingTicketAssigned")
	{
		if (!m_onMatchAssigned)
			return;
		MatchAssignment a;
		a.mode = GetStr(j, "$[0].mode");
		a.serverId = GetInt(j, "$[0].serverId");
		a.ticket = GetStr(j, "$[0].ticket");
		a.jti = GetStr(j, "$[0].jti");
		a.expiresAt = GetStr(j, "$[0].expiresAt");
		m_onMatchAssigned(a);
	}
	else if (target == "SessionEvicted")
	{
		if (!m_onSessionEvicted)
			return;
		SessionEvicted e;
		e.evictedServerId = GetInt(j, "$[0].evictedServerId");
		e.newServerId = GetInt(j, "$[0].newServerId");
		e.evictedTicketJti = GetStr(j, "$[0].evictedTicketJti");
		m_onSessionEvicted(e);
	}
	else if (target == "GlobalMessageReceived")
	{
		if (!m_onGlobalMessage)
			return;
		GlobalChatMessage m;
		m.messageId = GetStr(j, "$[0].messageId");
		m.authorSubject = GetStr(j, "$[0].authorSubject");
		m.authorUsername = GetStr(j, "$[0].authorUsername");
		m.text = GetStr(j, "$[0].text");
		m.sentAt = GetStr(j, "$[0].sentAt");
		m_onGlobalMessage(m);
	}
	else if (target == "GlobalMessageDeleted")
	{
		if (m_onGlobalMessageDeleted)
			m_onGlobalMessageDeleted(GetStr(j, "$[0].messageId"));
	}
	else if (target == "GlobalChatStateChanged")
	{
		if (m_onGlobalChatState)
			m_onGlobalChatState(GetInt(j, "$[0].slowModeSeconds"));
	}
	else if (target == "GlobalParticipantsChanged")
	{
		if (m_onGlobalParticipantsChanged)
			m_onGlobalParticipantsChanged();
	}
	else if (target == "FriendRequestReceived")
	{
		if (!m_onFriendRequest)
			return;
		FriendRequestEvent e;
		e.requestId = GetLong(j, "$[0].requestId");
		e.fromSubject = GetStr(j, "$[0].fromSubject");
		e.fromUsername = GetStr(j, "$[0].fromUsername");
		e.createdAt = GetStr(j, "$[0].createdAt");
		m_onFriendRequest(e);
	}
	else if (target == "FriendAdded")
	{
		if (m_onFriendAdded)
			m_onFriendAdded(GetStr(j, "$[0].subject"));
	}
	else if (target == "FriendRemoved")
	{
		if (m_onFriendRemoved)
			m_onFriendRemoved(GetStr(j, "$[0].subject"));
	}
	else if (target == "FriendRequestDeclined")
	{
		if (m_onFriendRequestResolved)
			m_onFriendRequestResolved(GetLong(j, "$[0].requestId"), false);
	}
	else if (target == "FriendRequestCancelled")
	{
		if (m_onFriendRequestResolved)
			m_onFriendRequestResolved(GetLong(j, "$[0].requestId"), true);
	}
	else if (target == "FriendPresenceChanged")
	{
		if (!m_onFriendPresenceChanged)
			return;
		FriendPresence p;
		p.subject = GetStr(j, "$[0].subject");
		p.status = GetStr(j, "$[0].status");
		p.serverId = GetInt(j, "$[0].serverId");
		m_onFriendPresenceChanged(p);
	}
}
