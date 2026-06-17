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
//   JSON string decoding. See json_util.h.
//
//-----------------------------------------------------------------------------

#include "json_util.h"

#include "mongoose.h"

namespace
{
void AppendUtf8(std::string& out, unsigned cp)
{
	if (cp <= 0x7F)
	{
		out += static_cast<char>(cp);
	}
	else if (cp <= 0x7FF)
	{
		out += static_cast<char>(0xC0 | (cp >> 6));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
	else if (cp <= 0xFFFF)
	{
		out += static_cast<char>(0xE0 | (cp >> 12));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
	else
	{
		out += static_cast<char>(0xF0 | (cp >> 18));
		out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
}

int HexVal(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

// Parse exactly 4 hex digits at p[0..3]; returns -1 on any non-hex digit.
int Hex4(const char* p)
{
	int v = 0;
	for (int k = 0; k < 4; ++k)
	{
		const int h = HexVal(p[k]);
		if (h < 0)
			return -1;
		v = (v << 4) | h;
	}
	return v;
}
} // namespace

std::string JsonGetStr(const struct mg_str& json, const char* path)
{
	struct mg_str tok = mg_json_get_tok(json, path);
	// Only quoted strings; null/number/absent -> empty (matches old behaviour).
	if (tok.len < 2 || tok.buf[0] != '"')
		return std::string();

	const char* s = tok.buf + 1;    // skip opening quote
	const size_t len = tok.len - 2; // drop both quotes
	std::string out;
	out.reserve(len);

	for (size_t i = 0; i < len;)
	{
		const char c = s[i];
		if (c != '\\')
		{
			out += c; // raw byte: literal UTF-8 passes straight through
			++i;
			continue;
		}
		if (i + 1 >= len)
			break; // dangling backslash
		const char e = s[i + 1];
		switch (e)
		{
		case '"': out += '"'; i += 2; break;
		case '\\': out += '\\'; i += 2; break;
		case '/': out += '/'; i += 2; break;
		case 'b': out += '\b'; i += 2; break;
		case 'f': out += '\f'; i += 2; break;
		case 'n': out += '\n'; i += 2; break;
		case 'r': out += '\r'; i += 2; break;
		case 't': out += '\t'; i += 2; break;
		case 'u':
		{
			if (i + 6 > len)
			{
				i = len; // not enough digits; stop
				break;
			}
			int cp = Hex4(s + i + 2);
			if (cp < 0)
			{
				i += 2; // malformed escape; skip the "\u"
				break;
			}
			i += 6;
			if (cp >= 0xD800 && cp <= 0xDBFF)
			{
				// High surrogate: pair with a following \uXXXX low surrogate.
				if (i + 6 <= len && s[i] == '\\' && s[i + 1] == 'u')
				{
					const int lo = Hex4(s + i + 2);
					if (lo >= 0xDC00 && lo <= 0xDFFF)
					{
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						i += 6;
					}
					else
					{
						cp = 0xFFFD; // unpaired high surrogate
					}
				}
				else
				{
					cp = 0xFFFD;
				}
			}
			else if (cp >= 0xDC00 && cp <= 0xDFFF)
			{
				cp = 0xFFFD; // lone low surrogate
			}
			AppendUtf8(out, static_cast<unsigned>(cp));
			break;
		}
		default:
			out += e; // unknown escape: take the char literally
			i += 2;
			break;
		}
	}
	return out;
}
