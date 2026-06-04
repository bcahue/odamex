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
//   Launcher DPoP key (shipping plan C3). A long-lived P-256 (ES256) key pair
//   used to prove possession on every API call. The key is generated on first
//   launch and persisted in the OS keystore; the private key never leaves it.
//   The same key's public JWK is registered at /api/launcher/auth/start and
//   embedded in every DPoP proof (C6), so the API binds the session to its
//   RFC 7638 thumbprint.
//
//   Windows: an NCrypt persisted key in the Microsoft Software Key Storage
//   Provider (DPAPI-protected per user). macOS/Linux backends are stubbed for
//   a later wave (Keychain / libsecret).
//
//-----------------------------------------------------------------------------

#pragma once

#include <memory>
#include <string>

class DpopKey
{
  public:
	DpopKey();
	~DpopKey();

	DpopKey(const DpopKey&) = delete;
	DpopKey& operator=(const DpopKey&) = delete;

	// Open the persisted key, generating and storing it on first run. Returns
	// false if the platform backend is unavailable or the operation fails.
	bool LoadOrCreate();

	// True once a key is loaded and usable.
	bool IsReady() const;

	// The public key as a JWK object string:
	//   {"kty":"EC","crv":"P-256","x":"<b64url>","y":"<b64url>"}
	// Empty when not ready.
	std::string PublicJwkJson() const;

	// Sign `message` with ES256 (SHA-256 then ECDSA P-256). Returns the raw JWS
	// signature (r||s, 64 bytes) base64url-encoded with no padding -- ready to
	// be the third segment of a compact JWS. Empty on failure. Used by C6.
	std::string SignEs256(const std::string& message) const;

  private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
