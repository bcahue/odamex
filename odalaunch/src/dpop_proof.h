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
//   DPoP proof construction (RFC 9449, shipping plan C6). Builds the compact
//   JWS sent in the `DPoP` HTTP header to prove possession of the launcher's
//   DPoP key on every protected API call. Header is
//   {typ: dpop+jwt, alg: ES256, jwk: <public key>}; claims are
//   {htm, htu, iat, jti, [ath]}. Signed with DpopKey::SignEs256.
//
//-----------------------------------------------------------------------------

#pragma once

#include <string>

class DpopKey;

namespace DpopProof
{
// Build a DPoP proof JWT for one request.
//   key         - the launcher DPoP key (must be IsReady()); its public JWK is
//                 embedded and it signs the proof.
//   httpMethod  - request method, e.g. "POST" / "GET" (becomes the `htm` claim).
//   httpUrl     - full request URL; the query/fragment is stripped for `htu`
//                 to match the API's htu comparison (scheme+host+port+path).
//   accessToken - when non-empty, adds the `ath` claim
//                 (base64url(SHA-256(accessToken))) binding the proof to that
//                 token. Pass empty for unauthenticated requests.
// Returns the compact JWS string, or empty on failure.
std::string Create(const DpopKey& key, const std::string& httpMethod,
                   const std::string& httpUrl,
                   const std::string& accessToken = std::string());
} // namespace DpopProof
