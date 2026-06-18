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
//   Local, client-side profanity masking for chat display. Whole-word matching
//   against a built-in list; matched ASCII words are replaced with asterisks of
//   the same length. Non-letter bytes -- digits, punctuation, whitespace, emoji
//   and other UTF-8 -- pass through untouched, so only ASCII profanity tokens
//   are masked (no markup/Unicode corruption). Opt-out via a settings toggle.
//
//-----------------------------------------------------------------------------

#pragma once

#include <string>

// Return a copy of `text` (UTF-8) with whole-word profanity replaced by '*'.
std::string CensorProfanity(const std::string& text);
