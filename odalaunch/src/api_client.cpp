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
//   Authenticated REST client. See api_client.h.
//
//-----------------------------------------------------------------------------

#include "api_client.h"

#include <wx/webrequest.h>

#include <string>

#include "dpop_key.h"
#include "dpop_proof.h"

namespace
{
// Minimal JSON string escaper for request bodies. Subjects are URL-safe ids in
// practice, but escape defensively so a stray character can't break the body.
std::string JsonString(const std::string& s)
{
	std::string out = "\"";
	for (char c : s)
	{
		switch (c)
		{
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default: out += c;
		}
	}
	out += "\"";
	return out;
}

// {"targetSubject":"<sub>"}
std::string TargetBody(const std::string& sub)
{
	return "{\"targetSubject\":" + JsonString(sub) + "}";
}
} // namespace

ApiClient::ApiClient(wxString apiBaseUrl,
                     std::function<std::string()> tokenProvider, const DpopKey& key)
    : m_apiBaseUrl(std::move(apiBaseUrl)), m_tokenProvider(std::move(tokenProvider)),
      m_key(key)
{
}

ApiClient::Response ApiClient::Send(const char* method, const wxString& path,
                                    const std::string& jsonBody)
{
	Response r;

	const wxString url = m_apiBaseUrl + path;
	const std::string urlUtf8 = url.utf8_string();
	const std::string token = m_tokenProvider();

	// DPoP proof bound to this request + the session token (ath claim).
	const std::string proof = DpopProof::Create(m_key, method, urlUtf8, token);
	if (proof.empty())
		return r;

	wxWebSessionSync& session = wxWebSessionSync::GetDefault();
	wxWebRequestSync request = session.CreateRequest(url);
	if (!request.IsOk())
		return r;

	request.SetMethod(method);
	if (!jsonBody.empty())
		request.SetData(wxString::FromUTF8(jsonBody), "application/json");
	request.SetHeader("Authorization", "Bearer " + wxString::FromUTF8(token));
	request.SetHeader("DPoP", wxString::FromUTF8(proof));

	wxWebRequestSync::Result result = request.Execute();
	if (result.state != wxWebRequest::State_Completed)
		return r;

	wxWebResponse response = request.GetResponse();
	r.status = response.GetStatus();
	r.body = response.AsString().utf8_string();
	r.ok = (r.status >= 200 && r.status < 300);
	return r;
}

// ---- reads ----

ApiClient::Response ApiClient::GetFriends()
{
	return Send("GET", "/api/friends", std::string());
}

ApiClient::Response ApiClient::GetFriendRequests(const std::string& direction)
{
	return Send("GET", "/api/friends/requests?direction=" + wxString::FromUTF8(direction),
	            std::string());
}

ApiClient::Response ApiClient::GetBlocks()
{
	return Send("GET", "/api/friends/blocks", std::string());
}

ApiClient::Response ApiClient::GetGlobalChatHistory()
{
	return Send("GET", "/api/chat/global/messages", std::string());
}

ApiClient::Response ApiClient::GetParticipants()
{
	return Send("GET", "/api/chat/global/participants", std::string());
}

// ---- mutations ----

ApiClient::Response ApiClient::SendFriendRequest(const std::string& targetSubject)
{
	return Send("POST", "/api/friends/requests", TargetBody(targetSubject));
}

ApiClient::Response ApiClient::AcceptFriendRequest(long long requestId)
{
	const std::string p = "/api/friends/requests/" + std::to_string(requestId) + "/accept";
	return Send("POST", wxString::FromUTF8(p), std::string());
}

ApiClient::Response ApiClient::DeclineFriendRequest(long long requestId)
{
	const std::string p = "/api/friends/requests/" + std::to_string(requestId) + "/decline";
	return Send("POST", wxString::FromUTF8(p), std::string());
}

ApiClient::Response ApiClient::CancelFriendRequest(long long requestId)
{
	const std::string p = "/api/friends/requests/" + std::to_string(requestId);
	return Send("DELETE", wxString::FromUTF8(p), std::string());
}

ApiClient::Response ApiClient::RemoveFriend(const std::string& subject)
{
	// Subjects are Keycloak UUIDs (URL-safe) so they're appended directly.
	return Send("DELETE", "/api/friends/" + wxString::FromUTF8(subject), std::string());
}

ApiClient::Response ApiClient::Block(const std::string& targetSubject)
{
	return Send("POST", "/api/friends/blocks", TargetBody(targetSubject));
}

ApiClient::Response ApiClient::Unblock(const std::string& subject)
{
	return Send("DELETE", "/api/friends/blocks/" + wxString::FromUTF8(subject), std::string());
}
