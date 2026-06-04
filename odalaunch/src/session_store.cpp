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
//   Launcher session token persistence. See session_store.h.
//
//-----------------------------------------------------------------------------

#include "session_store.h"

// ===========================================================================
#ifdef _WIN32
// ===========================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <wincred.h>

namespace
{
// Credential Manager target name. Stable across launches; changing it orphans
// any stored token (harmless -- the user just signs in again).
const wchar_t* const kTargetName = L"Odamex/LauncherSession";
} // namespace

bool SessionStore::Save(const std::string& token)
{
	if (token.empty() || token.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
		return false;

	CREDENTIALW cred;
	ZeroMemory(&cred, sizeof(cred));
	cred.Type = CRED_TYPE_GENERIC;
	cred.TargetName = const_cast<LPWSTR>(kTargetName);
	cred.CredentialBlobSize = static_cast<DWORD>(token.size());
	cred.CredentialBlob =
	    reinterpret_cast<LPBYTE>(const_cast<char*>(token.data()));
	// Persist for this user on this machine (DPAPI-protected at rest).
	cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

	return CredWriteW(&cred, 0) == TRUE;
}

bool SessionStore::Load(std::string& token)
{
	PCREDENTIALW pcred = nullptr;
	if (CredReadW(kTargetName, CRED_TYPE_GENERIC, 0, &pcred) != TRUE)
		return false;

	bool ok = false;
	if (pcred != nullptr)
	{
		if (pcred->CredentialBlob != nullptr && pcred->CredentialBlobSize > 0)
		{
			token.assign(reinterpret_cast<const char*>(pcred->CredentialBlob),
			             pcred->CredentialBlobSize);
			ok = true;
		}
		CredFree(pcred);
	}
	return ok;
}

bool SessionStore::Clear()
{
	if (CredDeleteW(kTargetName, CRED_TYPE_GENERIC, 0) == TRUE)
		return true;

	// Already absent is success for our purposes; anything else is a failure.
	return GetLastError() == ERROR_NOT_FOUND;
}

// ===========================================================================
#else // non-Windows: stub until macOS Keychain / Linux libsecret backends land
// ===========================================================================

bool SessionStore::Save(const std::string& /*token*/)
{
	return false;
}

bool SessionStore::Load(std::string& /*token*/)
{
	return false;
}

bool SessionStore::Clear()
{
	return false;
}

#endif
