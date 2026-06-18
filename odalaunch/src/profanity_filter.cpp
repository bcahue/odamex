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
//   Profanity masking. See profanity_filter.h. The word/phrase data is the
//   LDNOOBW English list (CC-BY-4.0), vendored in profanity_words.inc.
//
//-----------------------------------------------------------------------------

#include "profanity_filter.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
inline bool IsAsciiAlpha(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

inline bool IsAsciiAlnum(unsigned char c)
{
	return IsAsciiAlpha(c) || (c >= '0' && c <= '9');
}

inline char ToLowerAscii(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
}

bool IsPureAlpha(const std::string& s)
{
	if (s.empty())
		return false;
	for (const unsigned char c : s)
		if (!IsAsciiAlpha(c))
			return false;
	return true;
}

struct Blocklist
{
	// Single ASCII-letter words: matched as whole tokens (no substring hits, so
	// "class"/"assassin" are safe). Everything else (multi-word phrases, entries
	// with digits/punctuation) is matched as a boundary-aware substring.
	std::unordered_set<std::string> tokens;
	std::vector<std::string> phrases;
};

const Blocklist& Data()
{
	static const Blocklist data = [] {
		static const char* const kEntries[] = {
#include "profanity_words.inc"
		};

		Blocklist d;
		for (const char* entry : kEntries)
		{
			std::string w(entry); // already lowercase in the data file
			if (IsPureAlpha(w))
				d.tokens.insert(std::move(w));
			else
				d.phrases.push_back(std::move(w));
		}
		// Longest phrases first so the most specific span is masked.
		std::sort(d.phrases.begin(), d.phrases.end(),
		          [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
		return d;
	}();
	return data;
}
} // namespace

std::string CensorProfanity(const std::string& text)
{
	const size_t n = text.size();
	if (n == 0)
		return text;

	const Blocklist& data = Data();

	// ASCII-lowercased view for matching; byte positions stay aligned with `text`
	// (only A-Z change, never length), and non-ASCII bytes are left untouched.
	std::string lower(text);
	for (char& c : lower)
		c = ToLowerAscii(static_cast<unsigned char>(c));

	// 1 => replace this byte with '*'. Matches only ever cover ASCII bytes, so we
	// never split a multi-byte UTF-8 sequence (emoji etc. survive intact).
	std::vector<char> mask(n, 0);

	// Phrase pass: boundary-aware, case-insensitive substring matches.
	for (const std::string& p : data.phrases)
	{
		if (p.size() > n)
			continue;
		size_t pos = 0;
		while ((pos = lower.find(p, pos)) != std::string::npos)
		{
			const size_t end = pos + p.size();
			const bool leftOk = (pos == 0) || !IsAsciiAlnum(static_cast<unsigned char>(lower[pos - 1]));
			const bool rightOk = (end == n) || !IsAsciiAlnum(static_cast<unsigned char>(lower[end]));
			if (leftOk && rightOk)
			{
				for (size_t i = pos; i < end; ++i)
					mask[i] = 1;
				pos = end;
			}
			else
			{
				pos += 1;
			}
		}
	}

	// Token pass: whole ASCII-letter runs that are in the word set.
	size_t i = 0;
	while (i < n)
	{
		if (!IsAsciiAlpha(static_cast<unsigned char>(lower[i])))
		{
			++i;
			continue;
		}
		const size_t start = i;
		while (i < n && IsAsciiAlpha(static_cast<unsigned char>(lower[i])))
			++i;
		if (data.tokens.count(lower.substr(start, i - start)) != 0)
			for (size_t k = start; k < i; ++k)
				mask[k] = 1;
	}

	std::string out;
	out.reserve(n);
	for (size_t k = 0; k < n; ++k)
		out += mask[k] ? '*' : text[k];
	return out;
}
