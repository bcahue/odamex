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
//   Player authentication via JWT game tickets.
//   Fetches JWKS from the Odamex API and verifies ES256-signed tickets.
//
//-----------------------------------------------------------------------------

#include "odamex.h"

#include "sv_auth.h"
#include "i_system.h"

#include <chrono>
#include <map>
#include <memory>
#include <sstream>
#include <string>

#include <curl/curl.h>
#include "json/json.h"

// wolfSSL provides the crypto backend for jwt-cpp via its OpenSSL-compat layer.
// options.h must come first (it carries the build config), and we include the
// compat <openssl/engine.h> explicitly: jwt-cpp references the OpenSSL ENGINE
// type but never includes that header, and wolfSSL keeps it in its compat tree
// rather than pulling it in transitively the way real OpenSSL does.
#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#ifdef LIBWOLFSSL_VERSION_HEX
#include <openssl/engine.h>
#endif

#include "jwt-cpp/traits/open-source-parsers-jsoncpp/defaults.h"

EXTERN_CVAR(sv_auth_enabled)
EXTERN_CVAR(sv_auth_api_url)
EXTERN_CVAR(sv_auth_server_id)

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

struct CachedKey
{
	std::string kid;
	std::string pem;
};

static std::map<std::string, CachedKey> s_keyCache;
static bool s_ready = false;
static std::chrono::steady_clock::time_point s_lastFetchTime;
static std::chrono::steady_clock::time_point s_lastRetryTime;
static bool s_pendingRetry = false;

static const auto REFRESH_INTERVAL = std::chrono::hours(24);
static const auto RETRY_INTERVAL = std::chrono::minutes(5);

// Replay tracking (B4): jti -> unix time after which the entry may be pruned.
// A ticket's jti is single-use until its own exp lapses; we keep it a little
// past exp to cover clock skew / the verifier's leeway.
static std::map<std::string, int64_t> s_seenJtis;
static const int64_t JTI_GRACE_SECONDS = 60;

// ---------------------------------------------------------------------------
// curl helpers
// ---------------------------------------------------------------------------

static size_t CurlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
	std::string* response = static_cast<std::string*>(userdata);
	size_t totalBytes = size * nmemb;
	response->append(ptr, totalBytes);
	return totalBytes;
}

static bool FetchJWKS()
{
	std::string url = std::string(sv_auth_api_url.cstring()) + "/.well-known/jwks.json";

	CURL* curl = curl_easy_init();
	if (!curl) {
		PrintFmt(PRINT_HIGH, "SV_Auth: Failed to initialize curl handle\n");
		return false;
	}

	std::string response;
	std::string useragent = std::string("Odamex/") + DOTVERSIONSTR;

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, useragent.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		PrintFmt(PRINT_HIGH, "SV_Auth: JWKS fetch failed: {}\n", curl_easy_strerror(res));
		return false;
	}

	// Parse JSON response
	Json::Value root;
	Json::CharReaderBuilder builder;
	std::string errs;
	std::istringstream stream(response);

	if (!Json::parseFromStream(builder, stream, &root, &errs)) {
		PrintFmt(PRINT_HIGH, "SV_Auth: Failed to parse JWKS JSON: {}\n", errs);
		return false;
	}

	if (!root.isMember("keys") || !root["keys"].isArray()) {
		PrintFmt(PRINT_HIGH, "SV_Auth: JWKS response missing 'keys' array\n");
		return false;
	}

	std::map<std::string, CachedKey> newCache;

	const Json::Value& keys = root["keys"];
	for (Json::ArrayIndex i = 0; i < keys.size(); ++i) {
		const Json::Value& key = keys[i];

		if (!key.isMember("kty") || !key.isMember("alg") || !key.isMember("crv") ||
		    !key.isMember("kid") || !key.isMember("x") || !key.isMember("y"))
			continue;

		std::string kty = key["kty"].asString();
		std::string alg = key["alg"].asString();
		std::string crv = key["crv"].asString();

		if (kty != "EC" || alg != "ES256" || crv != "P-256")
			continue;

		std::string kid = key["kid"].asString();
		std::string x = key["x"].asString();
		std::string y = key["y"].asString();

		try {
			std::string pem = jwt::helper::create_public_key_from_ec_components("P-256", x, y);
			CachedKey cached;
			cached.kid = kid;
			cached.pem = pem;
			newCache[kid] = cached;
		} catch (const std::exception& e) {
			PrintFmt(PRINT_HIGH, "SV_Auth: Failed to create PEM for kid '{}': {}\n", kid, e.what());
		}
	}

	if (newCache.empty()) {
		PrintFmt(PRINT_HIGH, "SV_Auth: No valid EC P-256 keys found in JWKS\n");
		return false;
	}

	s_keyCache = newCache;
	s_lastFetchTime = std::chrono::steady_clock::now();
	s_pendingRetry = false;

	PrintFmt(PRINT_HIGH, "SV_Auth: Loaded {} signing key(s) from JWKS\n", s_keyCache.size());
	return true;
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void SV_AuthInit()
{
	if (!sv_auth_enabled)
		return;

	if (strlen(sv_auth_api_url.cstring()) == 0)
		I_FatalError("SV_Auth: sv_auth_api_url must be set when sv_auth_enabled is true");

	if (sv_auth_server_id.asInt() == 0)
		I_FatalError("SV_Auth: sv_auth_server_id must be set when sv_auth_enabled is true");

	curl_global_init(CURL_GLOBAL_DEFAULT);

	if (!FetchJWKS())
		I_FatalError("SV_Auth: Failed to fetch JWKS from {}", sv_auth_api_url.cstring());

	s_ready = true;
	PrintFmt(PRINT_HIGH, "SV_Auth: Authentication system initialized\n");
}

void SV_AuthTick()
{
	if (!sv_auth_enabled || !s_ready)
		return;

	auto now = std::chrono::steady_clock::now();

	if (s_pendingRetry) {
		if ((now - s_lastRetryTime) >= RETRY_INTERVAL) {
			PrintFmt(PRINT_HIGH, "SV_Auth: Retrying JWKS fetch...\n");
			if (!FetchJWKS()) {
				s_lastRetryTime = now;
				PrintFmt(PRINT_HIGH, "SV_Auth: JWKS retry failed, will retry in 5 minutes\n");
			}
		}
	} else if ((now - s_lastFetchTime) >= REFRESH_INTERVAL) {
		PrintFmt(PRINT_HIGH, "SV_Auth: Refreshing JWKS (24h elapsed)...\n");
		if (!FetchJWKS()) {
			s_pendingRetry = true;
			s_lastRetryTime = now;
			PrintFmt(PRINT_HIGH, "SV_Auth: JWKS refresh failed, retaining old keys, retry in 5 minutes\n");
		}
	}
}

TicketResult SV_AuthVerifyTicket(const std::string& token)
{
	TicketResult result;
	result.valid = false;
	result.srv = 0;
	result.expiresAt = 0;

	if (!SV_AuthReady()) {
		result.reason = "auth service unavailable";
		return result;
	}

	// Decode token
	std::unique_ptr<jwt::decoded_jwt<jwt::traits::open_source_parsers_jsoncpp>> decodedPtr;
	try {
		decodedPtr = std::make_unique<jwt::decoded_jwt<jwt::traits::open_source_parsers_jsoncpp>>(jwt::decode(token));
	} catch (const std::exception&) {
		result.reason = "invalid token format";
		return result;
	}
	auto& decoded = *decodedPtr;

	// Extract kid from header
	if (!decoded.has_key_id()) {
		result.reason = "invalid token format";
		return result;
	}
	std::string kid = decoded.get_key_id();

	// Look up kid in cache; if not found, try a single re-fetch
	if (s_keyCache.find(kid) == s_keyCache.end()) {
		FetchJWKS();
		if (s_keyCache.find(kid) == s_keyCache.end()) {
			result.reason = "unknown signing key";
			return result;
		}
	}

	// Build verifier and verify
	std::string audience = std::string("odamex-server-") + std::to_string(sv_auth_server_id.asInt());
	std::string issuer = sv_auth_api_url.cstring();

	auto verifyToken = [&](const std::string& pem) -> bool {
		try {
			auto verifier = jwt::verify()
				.allow_algorithm(jwt::algorithm::es256(pem))
				.with_issuer(issuer)
				.with_audience(audience)
				.leeway(30);
			verifier.verify(decoded);
			return true;
		} catch (const jwt::error::token_verification_exception& e) {
			std::string what = e.what();
			if (what.find("expired") != std::string::npos) {
				result.reason = "token expired";
			} else if (what.find("not valid yet") != std::string::npos ||
			           what.find("nbf") != std::string::npos) {
				result.reason = "token not yet valid";
			} else if (what.find("issuer") != std::string::npos ||
			           what.find("iss") != std::string::npos) {
				result.reason = "issuer mismatch";
			} else if (what.find("audience") != std::string::npos ||
			           what.find("aud") != std::string::npos) {
				result.reason = "audience mismatch";
			} else if (what.find("signature") != std::string::npos ||
			           what.find("verify") != std::string::npos) {
				result.reason = "signature verification failed";
			} else {
				result.reason = "signature verification failed";
			}
			return false;
		} catch (const std::exception&) {
			result.reason = "signature verification failed";
			return false;
		}
	};

	const std::string& pem = s_keyCache[kid].pem;

	if (!verifyToken(pem)) {
		// On signature failure, attempt a single JWKS re-fetch and retry
		if (result.reason == "signature verification failed") {
			if (FetchJWKS()) {
				if (s_keyCache.find(kid) != s_keyCache.end()) {
					result.reason.clear();
					if (!verifyToken(s_keyCache[kid].pem)) {
						return result;
					}
				} else {
					result.reason = "unknown signing key";
					return result;
				}
			} else {
				return result;
			}
		} else {
			return result;
		}
	}

	// Extract claims
	if (!decoded.has_subject() || decoded.get_subject().empty()) {
		result.reason = "missing required claim: sub";
		return result;
	}
	if (!decoded.has_id() || decoded.get_id().empty()) {
		result.reason = "missing required claim: jti";
		return result;
	}
	if (!decoded.has_payload_claim("srv")) {
		result.reason = "missing required claim: srv";
		return result;
	}

	result.sub = decoded.get_subject();
	result.jti = decoded.get_id();

	try {
		result.srv = decoded.get_payload_claim("srv").as_integer();
	} catch (const std::exception&) {
		result.reason = "missing required claim: srv";
		return result;
	}

	// Validate srv matches configured server id
	if (result.srv != sv_auth_server_id.asInt()) {
		result.reason = "server id mismatch";
		return result;
	}

	// Extract expiry
	auto exp = decoded.get_expires_at();
	result.expiresAt = std::chrono::system_clock::to_time_t(exp);

	result.valid = true;
	return result;
}

bool SV_AuthReady()
{
	return sv_auth_enabled && s_ready && !s_keyCache.empty();
}

bool SV_AuthRegisterJti(const std::string& jti, int64_t expiresAt)
{
	int64_t now = static_cast<int64_t>(time(NULL));

	// Sweep expired entries so the set stays bounded by the number of
	// currently-live tickets rather than growing without limit.
	for (auto it = s_seenJtis.begin(); it != s_seenJtis.end();) {
		if (it->second <= now)
			it = s_seenJtis.erase(it);
		else
			++it;
	}

	if (s_seenJtis.find(jti) != s_seenJtis.end())
		return false; // replay: already seen and not yet expired

	s_seenJtis[jti] = expiresAt + JTI_GRACE_SECONDS;
	return true;
}

void STACK_ARGS SV_AuthShutdown()
{
	s_keyCache.clear();
	s_seenJtis.clear();
	s_ready = false;
	s_pendingRetry = false;

	if (sv_auth_enabled)
		curl_global_cleanup();
}
