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
//   DPoP proof construction. See dpop_proof.h.
//
//-----------------------------------------------------------------------------

// Mongoose supplies the OS CSPRNG (mg_random, for the jti) and SHA-256
// (mg_sha256, for the ath claim). It's already linked to the launcher.
#include "mongoose.h"

#include "dpop_proof.h"

#include <cstdio>
#include <ctime>
#include <string>

#include "b64url.h"
#include "dpop_key.h"

namespace
{
// htu must be scheme://host:port/path with no query or fragment, matching the
// API's htu comparison.
std::string StripQueryAndFragment(const std::string& url)
{
	size_t cut = url.find_first_of("?#");
	return (cut == std::string::npos) ? url : url.substr(0, cut);
}

// Escape a string for inclusion in a JSON string literal. URLs and methods
// don't normally contain these, but htu is caller-supplied so we escape it
// rather than trust it.
std::string JsonEscape(const std::string& s)
{
	std::string out;
	out.reserve(s.size() + 8);
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
				std::snprintf(buf, sizeof(buf), "\\u%04x",
				              static_cast<unsigned char>(c));
				out += buf;
			}
			else
			{
				out += c;
			}
		}
	}
	return out;
}

// A fresh unique jti per proof: 16 CSPRNG bytes -> 22 base64url chars (well
// inside the API's accepted 8..128 length).
std::string RandomJti()
{
	unsigned char buf[16];
	mg_random(buf, sizeof(buf));
	return Base64UrlEncode(buf, sizeof(buf));
}

// ath = base64url(SHA-256(access_token)).
std::string AccessTokenHash(const std::string& accessToken)
{
	mg_sha256_ctx ctx;
	mg_sha256_init(&ctx);
	mg_sha256_update(&ctx,
	                 reinterpret_cast<const unsigned char*>(accessToken.data()),
	                 accessToken.size());
	unsigned char digest[32];
	mg_sha256_final(digest, &ctx);
	return Base64UrlEncode(digest, sizeof(digest));
}
} // namespace

namespace DpopProof
{
std::string Create(const DpopKey& key, const std::string& httpMethod,
                   const std::string& httpUrl, const std::string& accessToken)
{
	if (!key.IsReady())
		return std::string();

	const std::string jwk = key.PublicJwkJson();
	if (jwk.empty())
		return std::string();

	// The jwk value is already a JSON object string; splice it in verbatim.
	std::string header = "{\"typ\":\"dpop+jwt\",\"alg\":\"ES256\",\"jwk\":";
	header += jwk;
	header += "}";

	std::string payload = "{\"htm\":\"";
	payload += JsonEscape(httpMethod);
	payload += "\",\"htu\":\"";
	payload += JsonEscape(StripQueryAndFragment(httpUrl));
	payload += "\",\"iat\":";
	payload += std::to_string(static_cast<long long>(time(nullptr)));
	payload += ",\"jti\":\"";
	payload += RandomJti();
	payload += "\"";
	if (!accessToken.empty())
	{
		payload += ",\"ath\":\"";
		payload += AccessTokenHash(accessToken);
		payload += "\"";
	}
	payload += "}";

	std::string signingInput =
	    Base64UrlEncode(header) + "." + Base64UrlEncode(payload);

	std::string signature = key.SignEs256(signingInput);
	if (signature.empty())
		return std::string();

	return signingInput + "." + signature;
}
} // namespace DpopProof
