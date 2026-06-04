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
//   Launcher first-time registration completion. See launcher_registration.h.
//
//-----------------------------------------------------------------------------

// Mongoose for JSON build/parse + URL-encoding. Must precede wx headers so its
// <winsock2.h> wins the include-order race with wx's <windows.h>.
#include "mongoose.h"

#include "launcher_registration.h"

#include <string>

#include <wx/webrequest.h>

namespace
{
wxString TrimmedBase(const wxString& apiBaseUrl)
{
	wxString base = apiBaseUrl;
	while (base.EndsWith("/"))
		base.RemoveLast();
	return base;
}

// Read a string field out of a JSON body. Returns empty wxString if absent.
wxString JsonStr(const std::string& body, const char* path)
{
	struct mg_str j = mg_str_n(body.data(), body.size());
	char* v = mg_json_get_str(j, path);
	if (v == nullptr)
		return wxString();
	wxString out = wxString::FromUTF8(v);
	mg_free(v);
	return out;
}
} // namespace

LauncherRegistration::UsernameAvailability
LauncherRegistration::CheckUsername(const wxString& apiBaseUrl,
                                    const wxString& username)
{
	UsernameAvailability out; // defaults to Error

	// URL-encode the username into the ?value= query.
	const std::string nameUtf8 = username.utf8_string();
	std::string encoded(nameUtf8.size() * 3 + 1, '\0');
	size_t n = mg_url_encode(nameUtf8.data(), nameUtf8.size(), &encoded[0],
	                         encoded.size());
	encoded.resize(n);

	const wxString url = TrimmedBase(apiBaseUrl) +
	                     "/api/launcher/auth/username-available?value=" +
	                     wxString::FromUTF8(encoded);

	wxWebSessionSync& session = wxWebSessionSync::GetDefault();
	wxWebRequestSync request = session.CreateRequest(url);
	if (!request.IsOk())
		return out;

	wxWebRequestSync::Result result = request.Execute();
	if (result.state != wxWebRequest::State_Completed)
		return out;

	wxWebResponse response = request.GetResponse();
	if (response.GetStatus() != 200)
		return out;

	const std::string body = response.AsString().utf8_string();
	struct mg_str j = mg_str_n(body.data(), body.size());

	bool available = false;
	if (!mg_json_get_bool(j, "$.available", &available))
		return out; // malformed; leave as Error

	if (available)
	{
		out.status = UsernameAvailability::Status::Available;
		return out;
	}

	const wxString reason = JsonStr(body, "$.reason");
	if (reason == "taken")
		out.status = UsernameAvailability::Status::Taken;
	else if (reason == "invalid")
		out.status = UsernameAvailability::Status::Invalid;
	else
		out.status = UsernameAvailability::Status::Error;
	return out;
}

LauncherRegistration::RegistrationResult
LauncherRegistration::Complete(const wxString& apiBaseUrl,
                               const wxString& pendingToken,
                               const wxString& username)
{
	RegistrationResult out; // defaults to Failed

	// Build the JSON body, escaping both values.
	std::string body;
	{
		char* qp = mg_mprintf("%m", MG_ESC(pendingToken.utf8_string().c_str()));
		char* qu = mg_mprintf("%m", MG_ESC(username.utf8_string().c_str()));
		body = "{\"pendingToken\":";
		body += (qp != nullptr) ? qp : "\"\"";
		body += ",\"username\":";
		body += (qu != nullptr) ? qu : "\"\"";
		body += "}";
		mg_free(qp);
		mg_free(qu);
	}

	const wxString url =
	    TrimmedBase(apiBaseUrl) + "/api/launcher/auth/complete-registration";

	wxWebSessionSync& session = wxWebSessionSync::GetDefault();
	wxWebRequestSync request = session.CreateRequest(url);
	if (!request.IsOk())
	{
		out.error = "request_create_failed";
		return out;
	}

	request.SetMethod("POST");
	request.SetData(wxString::FromUTF8(body), "application/json");

	wxWebRequestSync::Result result = request.Execute();
	if (result.state != wxWebRequest::State_Completed)
	{
		out.error = result.error.empty() ? wxString("request_failed") : result.error;
		return out;
	}

	wxWebResponse response = request.GetResponse();
	const int status = response.GetStatus();
	const std::string respBody = response.AsString().utf8_string();

	if (status == 200)
	{
		out.sessionToken = JsonStr(respBody, "$.token");
		out.username = JsonStr(respBody, "$.username");
		out.status = out.sessionToken.empty()
		                 ? RegistrationResult::Status::Failed
		                 : RegistrationResult::Status::Success;
		if (out.sessionToken.empty())
			out.error = "missing_token_in_response";
		return out;
	}

	if (status == 409)
	{
		// Username taken: the API consumed the old pending token and issued a new
		// one so the user can pick again.
		out.status = RegistrationResult::Status::UsernameTaken;
		out.newPendingToken = JsonStr(respBody, "$.pendingToken");
		out.error = "username_taken";
		return out;
	}

	if (status == 400)
	{
		const wxString err = JsonStr(respBody, "$.error");
		out.error = err.empty() ? wxString("bad_request") : err;
		if (err == "invalid_username")
			out.status = RegistrationResult::Status::InvalidUsername;
		else if (err.Contains("pending_token"))
			out.status = RegistrationResult::Status::Expired;
		else
			out.status = RegistrationResult::Status::Failed;
		return out;
	}

	out.error = wxString::Format("http_%d", status);
	return out;
}
