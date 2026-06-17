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
//   JSON helpers for parsing server payloads. Mongoose's mg_json_get_str only
//   decodes \u00xx escapes within the ASCII range and truncates the string at
//   the first higher \uXXXX escape -- so any non-ASCII text (emoji, accents,
//   CJK, Cyrillic...) coming back as \u escapes is lost. JsonGetStr decodes the
//   raw token itself, handling \uXXXX plus UTF-16 surrogate pairs into UTF-8.
//
//-----------------------------------------------------------------------------

#pragma once

#include <string>

struct mg_str;

// Extract the JSON string at `path` (Mongoose path syntax), fully decoding
// escape sequences -- including \uXXXX and surrogate pairs -- to UTF-8. Returns
// an empty string if the value is absent or not a string.
std::string JsonGetStr(const struct mg_str& json, const char* path);
