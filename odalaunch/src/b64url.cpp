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
//   Base64url encoding. See b64url.h.
//
//-----------------------------------------------------------------------------

#include "b64url.h"

std::string Base64UrlEncode(const unsigned char* data, size_t len)
{
	static const char tbl[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	std::string out;
	out.reserve((len + 2) / 3 * 4);

	size_t i = 0;
	for (; i + 3 <= len; i += 3)
	{
		unsigned v = (static_cast<unsigned>(data[i]) << 16) |
		             (static_cast<unsigned>(data[i + 1]) << 8) |
		             static_cast<unsigned>(data[i + 2]);
		out += tbl[(v >> 18) & 0x3F];
		out += tbl[(v >> 12) & 0x3F];
		out += tbl[(v >> 6) & 0x3F];
		out += tbl[v & 0x3F];
	}

	if (len - i == 1)
	{
		unsigned v = static_cast<unsigned>(data[i]) << 16;
		out += tbl[(v >> 18) & 0x3F];
		out += tbl[(v >> 12) & 0x3F];
	}
	else if (len - i == 2)
	{
		unsigned v = (static_cast<unsigned>(data[i]) << 16) |
		             (static_cast<unsigned>(data[i + 1]) << 8);
		out += tbl[(v >> 18) & 0x3F];
		out += tbl[(v >> 12) & 0x3F];
		out += tbl[(v >> 6) & 0x3F];
	}

	return out;
}

std::string Base64UrlEncode(const std::string& s)
{
	return Base64UrlEncode(reinterpret_cast<const unsigned char*>(s.data()),
	                       s.size());
}
