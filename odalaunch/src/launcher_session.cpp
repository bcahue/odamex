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
//   Launcher account-session state. See launcher_session.h.
//
//-----------------------------------------------------------------------------

// Mongoose for JSON build/parse. Must precede wx headers so its <winsock2.h>
// wins the include-order race with wx's <windows.h>.
#include "mongoose.h"

#include "launcher_session.h"

#include <ctime>
#include <string>

#include "json_util.h"

#include "oda_defs.h"
#include "session_store.h"

#include <wx/fileconf.h>

namespace
{
// Decode a single base64url segment (no padding, '-'/'_' alphabet) into bytes.
// Returns empty on malformed input.
std::string Base64UrlDecode(const std::string& in)
{
	// Reverse lookup for the base64url alphabet; -1 marks invalid characters.
	signed char rev[256];
	for (int i = 0; i < 256; ++i)
		rev[i] = -1;
	const char* alpha =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	for (int i = 0; i < 64; ++i)
		rev[(unsigned char)alpha[i]] = (signed char)i;

	std::string out;
	out.reserve((in.size() / 4) * 3 + 3);

	int val = 0;
	int bits = 0;
	for (char c : in)
	{
		signed char d = rev[(unsigned char)c];
		if (d < 0)
			return std::string(); // invalid character
		val = (val << 6) | d;
		bits += 6;
		if (bits >= 8)
		{
			bits -= 8;
			out.push_back((char)((val >> bits) & 0xFF));
		}
	}
	return out;
}

// Pull the JWT payload (middle segment) out as decoded JSON bytes. Empty on
// failure.
std::string DecodeJwtPayload(const wxString& token)
{
	const std::string t = token.utf8_string();
	const size_t dot1 = t.find('.');
	if (dot1 == std::string::npos)
		return std::string();
	const size_t dot2 = t.find('.', dot1 + 1);
	if (dot2 == std::string::npos)
		return std::string();
	return Base64UrlDecode(t.substr(dot1 + 1, dot2 - dot1 - 1));
}
} // namespace

LauncherSession::LauncherSession()
    : m_keyReady(false), m_signedIn(false), m_forceLogin(false)
{
	wxFileConfig config;
	wxString url;
	config.Read(AUTHAPIURL, &url, ODA_AUTHAPIURL);
	config.Read(AUTHFORCELOGIN, &m_forceLogin, false);

	// There is deliberately no UI for this -- the auth endpoint is a single
	// project-wide URL, not a per-user preference. But persist the default on
	// first run so the key is discoverable in the config file for the devs /
	// alpha testers who need to repoint at a local or staging API by hand.
	if (!config.HasEntry(AUTHAPIURL))
	{
		config.Write(AUTHAPIURL, url);
		config.Flush();
	}

	while (url.EndsWith("/"))
		url.RemoveLast();
	m_apiBaseUrl = url;
}

LauncherSession::~LauncherSession() = default;

bool LauncherSession::EnsureKey()
{
	if (m_keyReady)
		return true;
	m_keyReady = m_key.LoadOrCreate();
	return m_keyReady;
}

wxString LauncherSession::UsernameFromToken(const wxString& token)
{
	const std::string payload = DecodeJwtPayload(token);
	if (payload.empty())
		return wxString();

	struct mg_str j = mg_str_n(payload.data(), payload.size());
	// JsonGetStr so non-ASCII usernames survive (Mongoose mangles \uXXXX > 0x7F).
	const std::string v = JsonGetStr(j, "$.preferred_username");
	if (v.empty())
		return wxString();
	return wxString::FromUTF8(v);
}

wxString LauncherSession::SubjectFromToken(const wxString& token)
{
	const std::string payload = DecodeJwtPayload(token);
	if (payload.empty())
		return wxString();

	struct mg_str j = mg_str_n(payload.data(), payload.size());
	const std::string v = JsonGetStr(j, "$.sub");
	if (v.empty())
		return wxString();
	return wxString::FromUTF8(v);
}

int64_t LauncherSession::ExpiryFromToken(const wxString& token)
{
	const std::string payload = DecodeJwtPayload(token);
	if (payload.empty())
		return 0;

	struct mg_str j = mg_str_n(payload.data(), payload.size());
	double exp = 0.0;
	if (!mg_json_get_num(j, "$.exp", &exp))
		return 0;
	return (int64_t)exp;
}

wxString LauncherSession::SerializeBlob() const
{
	char* qt = mg_mprintf("%m", MG_ESC(m_token.utf8_string().c_str()));
	char* qu = mg_mprintf("%m", MG_ESC(m_username.utf8_string().c_str()));
	std::string blob = "{\"token\":";
	blob += (qt != nullptr) ? qt : "\"\"";
	blob += ",\"username\":";
	blob += (qu != nullptr) ? qu : "\"\"";
	blob += "}";
	mg_free(qt);
	mg_free(qu);
	return wxString::FromUTF8(blob);
}

bool LauncherSession::ParseBlob(const std::string& blob, wxString& token,
                                wxString& username)
{
	struct mg_str j = mg_str_n(blob.data(), blob.size());

	char* t = mg_json_get_str(j, "$.token");
	if (t == nullptr)
	{
		// Back-compat: an older build may have stored the bare token string.
		if (!blob.empty() && blob[0] != '{')
		{
			token = wxString::FromUTF8(blob.data(), blob.size());
			username = UsernameFromToken(token);
			return !token.empty();
		}
		return false;
	}
	token = wxString::FromUTF8(t);
	mg_free(t);

	// JsonGetStr so non-ASCII usernames survive (Mongoose mangles \uXXXX > 0x7F).
	const std::string u = JsonGetStr(j, "$.username");
	if (!u.empty())
		username = wxString::FromUTF8(u);
	if (username.empty())
		username = UsernameFromToken(token);
	return !token.empty();
}

bool LauncherSession::Restore()
{
	std::string blob;
	if (!SessionStore::Load(blob) || blob.empty())
		return false;

	wxString token, username;
	if (!ParseBlob(blob, token, username))
	{
		SessionStore::Clear();
		return false;
	}

	// Drop an expired token so the UI doesn't show a stale "signed in" state.
	// ExpiryFromToken == 0 means "couldn't read exp" -- keep it and let the
	// first API call (which will 401) drive a re-login rather than wiping a
	// possibly-valid session here.
	const int64_t exp = ExpiryFromToken(token);
	if (exp != 0 && exp <= (int64_t)time(nullptr))
	{
		SessionStore::Clear();
		return false;
	}

	m_token = token;
	m_username = username;
	m_signedIn = true;
	return true;
}

bool LauncherSession::AdoptSession(const wxString& sessionToken,
                                   const wxString& username)
{
	m_token = sessionToken;
	m_username = username.empty() ? UsernameFromToken(sessionToken) : username;
	m_signedIn = !m_token.empty();

	if (!m_signedIn)
		return false;

	// A fresh sign-in succeeded: drop any pending force-login request.
	if (m_forceLogin)
		PersistForceLogin(false);

	return SessionStore::Save(SerializeBlob().utf8_string());
}

void LauncherSession::SignOut()
{
	m_signedIn = false;
	m_username.clear();
	m_token.clear();
	SessionStore::Clear();

	// Make the next sign-in prompt for credentials rather than silently reusing
	// the browser's Keycloak SSO session.
	PersistForceLogin(true);
}

void LauncherSession::PersistForceLogin(bool value)
{
	m_forceLogin = value;
	wxFileConfig config;
	config.Write(AUTHFORCELOGIN, value);
	config.Flush();
}
