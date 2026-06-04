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
//   Base64url (RFC 4648 §5) encoding without padding -- the encoding used by
//   JWK coordinates, JWS segments, and DPoP claims.
//
//-----------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <string>

// Encode `len` bytes at `data` as base64url with no '=' padding.
std::string Base64UrlEncode(const unsigned char* data, size_t len);

// Convenience overload for string payloads (JWS header/payload JSON).
std::string Base64UrlEncode(const std::string& s);
