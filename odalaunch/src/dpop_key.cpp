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
//   Launcher DPoP key. See dpop_key.h.
//
//-----------------------------------------------------------------------------

#include "dpop_key.h"

#include "b64url.h"

// ===========================================================================
#ifdef _WIN32
// ===========================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <bcrypt.h>
#include <ncrypt.h>

#include <vector>

namespace
{
// Stable name for the persisted key in the user's key store. Changing this
// orphans existing keys (users would re-register a fresh DPoP key on next login).
const wchar_t* const kKeyName = L"OdamexLauncherDPoP";

// SHA-256 via the multi-step BCrypt API (works on Vista+, unlike the one-shot
// BCryptHash which needs Win8.1+). Returns false on any failure.
bool Sha256(const unsigned char* data, size_t len, unsigned char out[32])
{
	BCRYPT_ALG_HANDLE hAlg = nullptr;
	if (!BCRYPT_SUCCESS(
	        BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
		return false;

	bool ok = false;
	BCRYPT_HASH_HANDLE hHash = nullptr;
	if (BCRYPT_SUCCESS(BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0)))
	{
		if (BCRYPT_SUCCESS(BCryptHashData(hHash, const_cast<PUCHAR>(data),
		                                  static_cast<ULONG>(len), 0)) &&
		    BCRYPT_SUCCESS(BCryptFinishHash(hHash, out, 32, 0)))
		{
			ok = true;
		}
		BCryptDestroyHash(hHash);
	}

	BCryptCloseAlgorithmProvider(hAlg, 0);
	return ok;
}
} // namespace

struct DpopKey::Impl
{
	NCRYPT_PROV_HANDLE prov = 0;
	NCRYPT_KEY_HANDLE key = 0;
};

DpopKey::DpopKey() : m_impl(new Impl) {}

DpopKey::~DpopKey()
{
	if (m_impl->key)
		NCryptFreeObject(m_impl->key);
	if (m_impl->prov)
		NCryptFreeObject(m_impl->prov);
}

bool DpopKey::LoadOrCreate()
{
	if (m_impl->key)
		return true; // already loaded

	NCRYPT_PROV_HANDLE prov = 0;
	if (FAILED(NCryptOpenStorageProvider(&prov, MS_KEY_STORAGE_PROVIDER, 0)))
		return false;

	NCRYPT_KEY_HANDLE key = 0;
	SECURITY_STATUS st = NCryptOpenKey(prov, &key, kKeyName, 0, 0);

	if (st == NTE_BAD_KEYSET || st == NTE_NO_KEY)
	{
		// First run: create and persist a fresh P-256 signing key.
		if (FAILED(NCryptCreatePersistedKey(prov, &key, BCRYPT_ECDSA_P256_ALGORITHM,
		                                     kKeyName, 0, 0)))
		{
			NCryptFreeObject(prov);
			return false;
		}
		if (FAILED(NCryptFinalizeKey(key, 0)))
		{
			NCryptFreeObject(key);
			NCryptFreeObject(prov);
			return false;
		}
	}
	else if (FAILED(st))
	{
		NCryptFreeObject(prov);
		return false;
	}

	m_impl->prov = prov;
	m_impl->key = key;
	return true;
}

bool DpopKey::IsReady() const
{
	return m_impl->key != 0;
}

std::string DpopKey::PublicJwkJson() const
{
	if (!m_impl->key)
		return std::string();

	// Export the public key as a BCRYPT_ECCKEY_BLOB: a header followed by the
	// X and Y coordinates, each cbKey bytes (32 for P-256).
	DWORD cb = 0;
	if (FAILED(NCryptExportKey(m_impl->key, 0, BCRYPT_ECCPUBLIC_BLOB, nullptr,
	                           nullptr, 0, &cb, 0)) ||
	    cb < sizeof(BCRYPT_ECCKEY_BLOB))
		return std::string();

	std::vector<BYTE> blob(cb);
	if (FAILED(NCryptExportKey(m_impl->key, 0, BCRYPT_ECCPUBLIC_BLOB, nullptr,
	                           blob.data(), cb, &cb, 0)))
		return std::string();

	const BCRYPT_ECCKEY_BLOB* hdr =
	    reinterpret_cast<const BCRYPT_ECCKEY_BLOB*>(blob.data());
	DWORD coordLen = hdr->cbKey;
	if (sizeof(BCRYPT_ECCKEY_BLOB) + 2u * coordLen > cb)
		return std::string();

	const BYTE* x = blob.data() + sizeof(BCRYPT_ECCKEY_BLOB);
	const BYTE* y = x + coordLen;

	std::string out = "{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"";
	out += Base64UrlEncode(x, coordLen);
	out += "\",\"y\":\"";
	out += Base64UrlEncode(y, coordLen);
	out += "\"}";
	return out;
}

std::string DpopKey::SignEs256(const std::string& message) const
{
	if (!m_impl->key)
		return std::string();

	unsigned char hash[32];
	if (!Sha256(reinterpret_cast<const unsigned char*>(message.data()),
	            message.size(), hash))
		return std::string();

	// ECDSA signatures from CNG are the raw r||s concatenation (64 bytes for
	// P-256), which is exactly the JWS ES256 wire format. No padding info is
	// used for ECDSA.
	DWORD cbSig = 0;
	if (FAILED(NCryptSignHash(m_impl->key, nullptr, hash, sizeof(hash), nullptr, 0,
	                          &cbSig, 0)))
		return std::string();

	std::vector<BYTE> sig(cbSig);
	if (FAILED(NCryptSignHash(m_impl->key, nullptr, hash, sizeof(hash), sig.data(),
	                          cbSig, &cbSig, 0)))
		return std::string();

	return Base64UrlEncode(sig.data(), cbSig);
}

// ===========================================================================
#else // non-Windows: stub until macOS Keychain / Linux libsecret backends land
// ===========================================================================

struct DpopKey::Impl
{
};

DpopKey::DpopKey() : m_impl(new Impl) {}
DpopKey::~DpopKey() = default;

bool DpopKey::LoadOrCreate()
{
	return false;
}

bool DpopKey::IsReady() const
{
	return false;
}

std::string DpopKey::PublicJwkJson() const
{
	return std::string();
}

std::string DpopKey::SignEs256(const std::string& /*message*/) const
{
	return std::string();
}

#endif
