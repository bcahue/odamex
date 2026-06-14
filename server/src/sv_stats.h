// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id$
//
// Copyright (C) 1998-2006 by Randy Heit (ZDoom).
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
// Player statistics generation
//
//-----------------------------------------------------------------------------

#pragma once

#include <string>

// ===========================================================================
// Match-stats serialization + upload (B7 / S6).
//
// This module turns the in-memory results of a finished match into the v6
// GameV6 JSON the API expects and hands it to the async API client for upload.
// See the WDLStats JSON upload plan, Phase S (S5 = serialize, S6 = upload).
// ===========================================================================

// Serialize the current/just-finished match into the v6 GameV6 JSON blob, or
// "" if nothing was recorded.
std::string SV_SerializeMatchStats();

// Entry point called at match end (from M_CommitWDLLog, server build): serialize
// the finished match on the main thread, then hand the blob to the async
// uploader (SV_ApiUploadMatchStats) for delivery off the game loop. durationTics
// is the match length (gametic - begintic), used to derive the match window.
void SV_UploadMatchStats(int durationTics);

