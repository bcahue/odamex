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
//   Social orchestrator. See social_controller.h.
//
//-----------------------------------------------------------------------------

#include "mongoose.h"

#include "social_controller.h"

#include <algorithm>
#include <wx/event.h>

#include "api_client.h"
#include "json_util.h"
#include "party_client.h"

namespace
{
const size_t kChatCap = 200;

std::string GetStr(const struct mg_str& j, const std::string& path)
{
	// JsonGetStr (not mg_json_get_str) so non-ASCII chat text / usernames
	// survive: Mongoose truncates at the first \uXXXX > 0x7F.
	return JsonGetStr(j, path.c_str());
}

int GetInt(const struct mg_str& j, const std::string& path)
{
	return (int)mg_json_get_long(j, path.c_str(), 0);
}

long long GetLong(const struct mg_str& j, const std::string& path)
{
	return (long long)mg_json_get_long(j, path.c_str(), 0);
}

// "$.<arr>[<i>].<field>"
std::string P(const char* arr, int i, const char* field)
{
	return std::string("$.") + arr + "[" + std::to_string(i) + "]." + field;
}

// True while element <i> of <arr> exists (probe a required string field).
bool Has(const struct mg_str& j, const char* arr, int i, const char* probe)
{
	const std::string path = P(arr, i, probe);
	char* v = mg_json_get_str(j, path.c_str());
	if (!v)
		return false;
	mg_free(v);
	return true;
}

void ParseFriends(const std::string& body, std::vector<SocialFriend>& out)
{
	struct mg_str j = mg_str_n(body.data(), body.size());
	for (int i = 0; Has(j, "friends", i, "subject"); ++i)
	{
		SocialFriend f;
		f.subject = GetStr(j, P("friends", i, "subject"));
		f.username = GetStr(j, P("friends", i, "username"));
		f.status = GetStr(j, P("friends", i, "status"));
		f.serverId = GetInt(j, P("friends", i, "serverId"));
		out.push_back(std::move(f));
	}
}

void ParseRequests(const std::string& body, std::vector<SocialRequest>& out)
{
	struct mg_str j = mg_str_n(body.data(), body.size());
	for (int i = 0; Has(j, "requests", i, "fromSubject"); ++i)
	{
		SocialRequest r;
		r.requestId = GetLong(j, P("requests", i, "requestId"));
		r.fromSubject = GetStr(j, P("requests", i, "fromSubject"));
		r.fromUsername = GetStr(j, P("requests", i, "fromUsername"));
		r.toSubject = GetStr(j, P("requests", i, "toSubject"));
		r.toUsername = GetStr(j, P("requests", i, "toUsername"));
		r.createdAt = GetStr(j, P("requests", i, "createdAt"));
		out.push_back(std::move(r));
	}
}

void ParseBlocks(const std::string& body, std::set<std::string>& out)
{
	struct mg_str j = mg_str_n(body.data(), body.size());
	for (int i = 0; Has(j, "blocks", i, "subject"); ++i)
		out.insert(GetStr(j, P("blocks", i, "subject")));
}

void ParseParticipants(const std::string& body, std::vector<SocialParticipant>& out)
{
	struct mg_str j = mg_str_n(body.data(), body.size());
	for (int i = 0; Has(j, "participants", i, "subject"); ++i)
	{
		SocialParticipant p;
		p.subject = GetStr(j, P("participants", i, "subject"));
		p.username = GetStr(j, P("participants", i, "username"));
		p.status = GetStr(j, P("participants", i, "status"));
		p.serverId = GetInt(j, P("participants", i, "serverId"));
		bool self = false;
		mg_json_get_bool(j, P("participants", i, "isSelf").c_str(), &self);
		p.isSelf = self;
		out.push_back(std::move(p));
	}
}

void ParseChat(const std::string& body, std::vector<SocialChatLine>& out)
{
	struct mg_str j = mg_str_n(body.data(), body.size());
	for (int i = 0; Has(j, "messages", i, "messageId"); ++i)
	{
		SocialChatLine c;
		c.messageId = GetStr(j, P("messages", i, "messageId"));
		c.authorSubject = GetStr(j, P("messages", i, "authorSubject"));
		c.authorUsername = GetStr(j, P("messages", i, "authorUsername"));
		c.text = GetStr(j, P("messages", i, "text"));
		c.sentAt = GetStr(j, P("messages", i, "sentAt"));
		out.push_back(std::move(c));
	}
}

void RemoveRequestById(std::vector<SocialRequest>& v, long long id)
{
	v.erase(std::remove_if(v.begin(), v.end(),
	                       [id](const SocialRequest& r) { return r.requestId == id; }),
	        v.end());
}
} // namespace

SocialController::SocialController(const wxString& apiBaseUrl,
                                   std::function<std::string()> tokenProvider,
                                   const DpopKey& key, wxEvtHandler* ui)
    : m_ui(ui), m_alive(std::make_shared<std::atomic<bool>>(true))
{
	m_api.reset(new ApiClient(apiBaseUrl, tokenProvider, key));
	m_hub.reset(new PartyClient(apiBaseUrl.utf8_string(), std::move(tokenProvider), key));
	WireHubEvents();
}

SocialController::~SocialController()
{
	Stop();
}

void SocialController::Start()
{
	if (m_running)
		return;
	m_running = true;
	m_worker = std::thread([this] { WorkerLoop(); });

	// Initial load (and re-load) whenever the hub (re)connects; surface the
	// connection state so the chat tab can show "connecting" / "disconnected".
	m_hub->SetOnConnected([this] {
		Marshal([this] {
			m_state.hubState = SocialState::HubState::Connected;
			m_state.NotifyChanged();
		});
		Enqueue([this] { DoRefreshAll(); });
	});
	m_hub->SetOnClosed([this](const std::string& /*reason*/, bool /*willRetry*/) {
		Marshal([this] {
			m_state.hubState = SocialState::HubState::Disconnected;
			m_state.NotifyChanged();
		});
	});
	m_hub->Start();
}

void SocialController::Stop()
{
	// Block any in-flight marshalled callbacks from touching us during teardown.
	if (m_alive)
		m_alive->store(false);

	if (m_hub)
		m_hub->Stop(); // joins the SignalR threads: no more hub callbacks

	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_running = false;
	}
	m_cv.notify_all();
	if (m_worker.joinable())
		m_worker.join();
}

void SocialController::Detach()
{
	// Block any in-flight marshalled callbacks from touching us during teardown.
	if (m_alive)
		m_alive->store(false);

	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_running = false;
	}
	m_cv.notify_all();

	// Detach rather than join: a sync REST call or a mid-connect WinHTTP socket
	// could otherwise block exit for the full network timeout. The caller leaks
	// this controller, so the detached threads keep valid pointers until the
	// process exits (which terminates them).
	if (m_hub)
		m_hub->Abandon();
	if (m_worker.joinable())
		m_worker.detach();
}

void SocialController::WireHubEvents()
{
	m_hub->SetOnGlobalMessage([this](const GlobalChatMessage& m) {
		Marshal([this, m] {
			SocialChatLine line;
			line.messageId = m.messageId;
			line.authorSubject = m.authorSubject;
			line.authorUsername = m.authorUsername;
			line.text = m.text;
			line.sentAt = m.sentAt;
			m_state.chat.push_back(std::move(line));
			if (m_state.chat.size() > kChatCap)
			{
				m_state.chat.erase(m_state.chat.begin(),
				                   m_state.chat.begin() +
				                       (m_state.chat.size() - kChatCap));
			}
			m_state.NotifyChanged();
		});
	});

	m_hub->SetOnGlobalMessageDeleted([this](const std::string& id) {
		Marshal([this, id] {
			for (auto& c : m_state.chat)
				if (c.messageId == id)
					c.deleted = true;
			m_state.NotifyChanged();
		});
	});

	m_hub->SetOnGlobalChatState([this](int secs) {
		Marshal([this, secs] {
			m_state.slowModeSeconds = secs;
			m_state.NotifyChanged();
		});
	});

	// Someone joined/left the channel: refresh just the participant roster.
	m_hub->SetOnGlobalParticipantsChanged(
	    [this] { Enqueue([this] { DoRefreshParticipants(); }); });

	m_hub->SetOnFriendRequest([this](const FriendRequestEvent& e) {
		Marshal([this, e] {
			for (const auto& r : m_state.incoming)
				if (r.requestId == e.requestId)
					return; // already have it
			SocialRequest r;
			r.requestId = e.requestId;
			r.fromSubject = e.fromSubject;
			r.fromUsername = e.fromUsername;
			r.createdAt = e.createdAt;
			m_state.incoming.push_back(std::move(r));
			m_state.NotifyChanged();
		});
	});

	// New friendship needs the username + presence the event doesn't carry, so
	// re-fetch. (Enqueue is thread-safe; called straight from the hub thread.)
	m_hub->SetOnFriendAdded([this](const std::string&) { Enqueue([this] { DoRefreshAll(); }); });

	m_hub->SetOnFriendRemoved([this](const std::string& sub) {
		Marshal([this, sub] {
			m_state.friends.erase(
			    std::remove_if(m_state.friends.begin(), m_state.friends.end(),
			                   [&sub](const SocialFriend& f) { return f.subject == sub; }),
			    m_state.friends.end());
			m_state.NotifyChanged();
		});
	});

	m_hub->SetOnFriendRequestResolved([this](long long id, bool /*cancelled*/) {
		Marshal([this, id] {
			RemoveRequestById(m_state.incoming, id);
			RemoveRequestById(m_state.outgoing, id);
			m_state.NotifyChanged();
		});
	});

	m_hub->SetOnFriendPresenceChanged([this](const FriendPresence& p) {
		Marshal([this, p] {
			for (auto& f : m_state.friends)
				if (f.subject == p.subject)
				{
					f.status = p.status;
					f.serverId = p.serverId;
				}
			for (auto& pt : m_state.participants)
				if (pt.subject == p.subject)
				{
					pt.status = p.status;
					pt.serverId = p.serverId;
				}
			m_state.NotifyChanged();
		});
	});
}

// ---- actions (UI thread) ----

void SocialController::RefreshAll()
{
	Enqueue([this] { DoRefreshAll(); });
}

void SocialController::SendGlobalMessage(const std::string& text)
{
	if (m_hub)
		m_hub->SendGlobalMessage(text, [](bool, const std::string&) {});
}

void SocialController::SendFriendRequest(const std::string& subject)
{
	Enqueue([this, subject] {
		m_api->SendFriendRequest(subject);
		DoRefreshAll();
	});
}

void SocialController::SendFriendRequestByUsername(
    const std::string& username,
    std::function<void(bool ok, const std::string& message)> done)
{
	Enqueue([this, username, done = std::move(done)] {
		ApiClient::Response r = m_api->SendFriendRequestByUsername(username);

		std::string message;
		if (r.ok)
		{
			message = "Friend request sent.";
			DoRefreshAll(); // reflect the new outgoing request (or auto-accept)
		}
		else
		{
			// Map the server's error code ({"error":"..."}) to friendly text.
			struct mg_str j = mg_str_n(r.body.data(), r.body.size());
			const std::string err = GetStr(j, "$.error");
			if (err == "TargetNotFound")
				message = "No player found with that username.";
			else if (err == "AlreadyFriends")
				message = "You're already friends with that player.";
			else if (err == "RequestAlreadyPending")
				message = "A request to that player is already pending.";
			else if (err == "SelfTarget")
				message = "You can't send a friend request to yourself.";
			else if (err == "Blocked")
				message = "Unable to send a request to that player.";
			else if (err == "FriendLimitReached")
				message = "Your friends list is full.";
			else if (r.status == 400)
				// 400 with no mapped error code is a validation rejection
				// (e.g. an empty/oversized name) -- the body is a problem-details
				// object, not our {"error":...} shape.
				message = "That username isn't valid.";
			else if (r.status == 401 || r.status == 403)
				message = "You're not signed in, or aren't allowed to do that.";
			else if (r.status == 0)
				message = "Couldn't reach the server.";
			else
				message = wxString::Format("Couldn't send the friend request (error %d).",
				                           r.status)
				              .utf8_string();
		}

		const bool ok = r.ok;
		if (done)
			Marshal([done, ok, message] { done(ok, message); });
	});
}

void SocialController::AcceptRequest(long long requestId)
{
	Enqueue([this, requestId] {
		m_api->AcceptFriendRequest(requestId);
		DoRefreshAll();
	});
}

void SocialController::DeclineRequest(long long requestId)
{
	Enqueue([this, requestId] {
		m_api->DeclineFriendRequest(requestId);
		DoRefreshAll();
	});
}

void SocialController::CancelRequest(long long requestId)
{
	Enqueue([this, requestId] {
		m_api->CancelFriendRequest(requestId);
		DoRefreshAll();
	});
}

void SocialController::RemoveFriend(const std::string& subject)
{
	Enqueue([this, subject] {
		m_api->RemoveFriend(subject);
		DoRefreshAll();
	});
}

void SocialController::Block(const std::string& subject)
{
	Enqueue([this, subject] {
		m_api->Block(subject);
		DoRefreshAll();
	});
}

void SocialController::Unblock(const std::string& subject)
{
	Enqueue([this, subject] {
		m_api->Unblock(subject);
		DoRefreshAll();
	});
}

// ---- worker plumbing ----

void SocialController::Enqueue(std::function<void()> work)
{
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		if (!m_running)
			return;
		m_tasks.push(std::move(work));
	}
	m_cv.notify_one();
}

void SocialController::WorkerLoop()
{
	for (;;)
	{
		std::function<void()> task;
		{
			std::unique_lock<std::mutex> lk(m_mutex);
			m_cv.wait(lk, [this] { return !m_running || !m_tasks.empty(); });
			if (!m_running && m_tasks.empty())
				return;
			task = std::move(m_tasks.front());
			m_tasks.pop();
		}
		task();
	}
}

void SocialController::Marshal(std::function<void()> uiWork)
{
	if (!m_ui)
		return;
	auto alive = m_alive;
	m_ui->CallAfter([alive, uiWork = std::move(uiWork)]() {
		if (alive && alive->load())
			uiWork();
	});
}

void SocialController::DoRefreshAll()
{
	std::vector<SocialFriend> friends;
	ApiClient::Response fr = m_api->GetFriends();
	if (fr.ok)
		ParseFriends(fr.body, friends);

	std::vector<SocialRequest> incoming;
	ApiClient::Response ir = m_api->GetFriendRequests("incoming");
	if (ir.ok)
		ParseRequests(ir.body, incoming);

	std::vector<SocialRequest> outgoing;
	ApiClient::Response orr = m_api->GetFriendRequests("outgoing");
	if (orr.ok)
		ParseRequests(orr.body, outgoing);

	std::set<std::string> blocked;
	ApiClient::Response br = m_api->GetBlocks();
	if (br.ok)
		ParseBlocks(br.body, blocked);

	std::vector<SocialParticipant> participants;
	ApiClient::Response pr = m_api->GetParticipants();
	if (pr.ok)
		ParseParticipants(pr.body, participants);

	std::vector<SocialChatLine> chat;
	ApiClient::Response cr = m_api->GetGlobalChatHistory();
	if (cr.ok)
		ParseChat(cr.body, chat);

	Marshal([this, friends = std::move(friends), incoming = std::move(incoming),
	         outgoing = std::move(outgoing), blocked = std::move(blocked),
	         participants = std::move(participants), chat = std::move(chat)]() mutable {
		m_state.friends = std::move(friends);
		m_state.incoming = std::move(incoming);
		m_state.outgoing = std::move(outgoing);
		m_state.blocked = std::move(blocked);
		m_state.participants = std::move(participants);
		m_state.chat = std::move(chat);
		m_state.NotifyChanged();
	});
}

void SocialController::DoRefreshParticipants()
{
	std::vector<SocialParticipant> participants;
	ApiClient::Response pr = m_api->GetParticipants();
	if (!pr.ok)
		return; // transient: keep the existing roster rather than blanking it
	ParseParticipants(pr.body, participants);

	Marshal([this, participants = std::move(participants)]() mutable {
		m_state.participants = std::move(participants);
		m_state.NotifyChanged();
	});
}
