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
//   Launcher OIDC login flow. See launcher_login.h.
//
//-----------------------------------------------------------------------------

// Mongoose supplies the small JSON helpers (parse + escape). It's already
// linked to the launcher for the loopback listener, so no new dependency.
// It MUST be included before any wx header: on Windows mongoose.h pulls in
// <winsock2.h>, which defines _WINSOCKAPI_ and stops wx's <windows.h> from
// dragging in the legacy <winsock.h> (the two can't coexist).
#include "mongoose.h"

#include "launcher_login.h"

#include <map>
#include <string>

#include <wx/app.h>
#include <wx/utils.h>       // wxLaunchDefaultBrowser
#include <wx/webrequest.h>

#include "oauth_listener.h"

namespace
{
LauncherLoginResult Fail(const wxString& code)
{
	LauncherLoginResult r;
	r.status = LauncherLoginResult::Status::Failed;
	r.error = code;
	return r;
}

// True if the URL targets a loopback host (localhost / 127.0.0.1 / [::1]).
bool IsLoopbackHost(const wxString& url)
{
	const wxString u = url.Lower();
	return u.StartsWith("http://localhost") || u.StartsWith("https://localhost") ||
	       u.StartsWith("http://127.0.0.1") || u.StartsWith("https://127.0.0.1") ||
	       u.StartsWith("http://[::1]") || u.StartsWith("https://[::1]");
}
} // namespace

LauncherLogin::LauncherLogin(const wxString& apiBaseUrl)
    : m_apiBaseUrl(apiBaseUrl), m_dpopJwkJson("{}"), m_hwidJson("{}")
{
	// Trim a trailing slash so we can concatenate paths cleanly.
	while (m_apiBaseUrl.EndsWith("/"))
		m_apiBaseUrl.RemoveLast();
}

void LauncherLogin::SetDpopPublicJwk(const wxString& jwkJson)
{
	m_dpopJwkJson = jwkJson.empty() ? wxString("{}") : jwkJson;
}

void LauncherLogin::SetHwidPayload(const wxString& hwidJson)
{
	m_hwidJson = hwidJson.empty() ? wxString("{}") : hwidJson;
}

LauncherLoginResult LauncherLogin::Run(int timeoutSeconds)
{
	// 1. Bring up the loopback listener so we have a concrete callback URL to
	//    register with /start.
	OAuthLoopbackListener listener;
	if (!listener.Start())
		return Fail("listener_bind_failed");

	const std::string callback = listener.RedirectUri();

	// 2. Build the /start request body. The callback is JSON-escaped via
	//    Mongoose's %m + MG_ESC; the DPoP JWK and HWID are already JSON objects
	//    and are spliced in verbatim.
	std::string body;
	{
		char* quotedCb = mg_mprintf("%m", MG_ESC(callback.c_str()));
		body = "{\"LauncherCallback\":";
		body += (quotedCb != nullptr) ? quotedCb : "\"\"";
		body += ",\"DpopJwk\":";
		body += m_dpopJwkJson.utf8_string();
		body += ",\"Hwid\":";
		body += m_hwidJson.utf8_string();
		if (m_forceLogin)
			body += ",\"ForceLogin\":true";
		body += "}";
		mg_free(quotedCb);
	}

	// 3. POST /api/launcher/auth/start. wxWebRequestSync needs no event loop and
	//    is safe on this worker thread; on Windows it uses the WinHTTP backend
	//    (HTTPS supported natively, no extra dependency).
	wxWebSessionSync& session = wxWebSessionSync::GetDefault();
	wxWebRequestSync request =
	    session.CreateRequest(m_apiBaseUrl + "/api/launcher/auth/start");
	if (!request.IsOk())
		return Fail("request_create_failed");

	// Dev convenience: the API's localhost run profile serves a self-signed
	// cert. Loopback traffic can't be meaningfully MITM'd, so relax peer
	// verification for localhost only -- this lets a developer point AuthApiUrl
	// straight at https://localhost:<port> and avoid the http->https redirect
	// that otherwise drops the POST body. Never relaxed for real hosts.
	// NO
	// We use https in dev like good little boys :)
	//if (IsLoopbackHost(m_apiBaseUrl))
	//request.MakeInsecure(wxWebRequestSync::Ignore_All);

	request.SetMethod("POST");
	request.SetData(wxString::FromUTF8(body), "application/json");

	wxWebRequestSync::Result result = request.Execute();
	if (result.state != wxWebRequest::State_Completed)
		return Fail(result.error.empty() ? wxString("start_request_failed")
		                                 : result.error);

	wxWebResponse response = request.GetResponse();
	if (response.GetStatus() != 200)
	{
		// Surface enough to tell apart the two common first-integration faults:
		//   sent=0                      -> the client transmitted no body
		//   sent>0 + "body required"    -> body was sent but dropped in transit
		//                                  (e.g. an http->https 307 redirect)
		const wxString respText = response.AsString();
		return Fail(wxString::Format(
		    "start_http_%d url=%s built=%llu sent=%lld bytes; resp=%s; body=%s",
		    response.GetStatus(), (m_apiBaseUrl + "/api/launcher/auth/start").c_str(),
		    (unsigned long long)body.size(), (long long)request.GetBytesSent(),
		    respText.c_str(), wxString::FromUTF8(body).c_str()));
	}

	// 4. Pull the authorize URL out of the JSON response.
	const std::string respBody = response.AsString().utf8_string();
	wxString authorizeUrl;
	{
		struct mg_str j = mg_str_n(respBody.data(), respBody.size());
		char* url = mg_json_get_str(j, "$.authorizeUrl");
		if (url == nullptr)
			return Fail("bad_start_response");
		authorizeUrl = wxString::FromUTF8(url);
		mg_free(url);
	}

	// 5. Open the authorize URL in the system browser. Marshal the GUI call to
	//    the main thread (Run() executes on a worker), which is the supported
	//    way to touch wx UI from a background thread.
	if (wxTheApp != nullptr)
	{
		wxString urlCopy = authorizeUrl;
		wxTheApp->CallAfter([urlCopy]() { wxLaunchDefaultBrowser(urlCopy); });
	}
	else
	{
		wxLaunchDefaultBrowser(authorizeUrl);
	}

	// 6. Block until the API bounces the browser back to our loopback listener,
	//    or the caller cancels (launcher shutting down).
	std::map<std::string, std::string> params;
	if (!listener.WaitForCallback(timeoutSeconds, params, m_cancel))
		return Fail((m_cancel != nullptr && m_cancel->load()) ? wxString("cancelled")
		                                                      : wxString("login_timeout"));

	// 7. Interpret the callback. The API redirects with exactly one of:
	//    token / pending / error (see LauncherAuthController.Callback).
	LauncherLoginResult out;

	auto tokenIt = params.find("token");
	auto pendingIt = params.find("pending");
	auto errorIt = params.find("error");

	if (tokenIt != params.end())
	{
		out.status = LauncherLoginResult::Status::Success;
		out.sessionToken = wxString::FromUTF8(tokenIt->second);
	}
	else if (pendingIt != params.end())
	{
		out.status = LauncherLoginResult::Status::PendingRegistration;
		out.pendingToken = wxString::FromUTF8(pendingIt->second);
	}
	else if (errorIt != params.end())
	{
		out.status = LauncherLoginResult::Status::Error;
		out.error = wxString::FromUTF8(errorIt->second);
		auto refIt = params.find("ref");
		if (refIt != params.end())
			out.reference = wxString::FromUTF8(refIt->second);
		auto reasonIt = params.find("reason");
		if (reasonIt != params.end())
			out.reason = wxString::FromUTF8(reasonIt->second);
	}
	else
	{
		out = Fail("no_callback_params");
	}

	listener.Stop();
	return out;
}
