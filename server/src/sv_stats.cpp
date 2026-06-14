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
// Player statistics generation — WDLStats v6 JSON upload (S5/S6). The match is
// compiled live during play and serialized in common (m_wdlstats_json.cpp); this
// is just the server's upload entry point.
//
//-----------------------------------------------------------------------------


#include "odamex.h"

#include "sv_stats.h"

#include <cstdint>
#include <ctime>

#include "m_wdlstats_agg.h"
#include "sv_apiclient.h"

std::string SV_SerializeMatchStats()
{
	return M_GetWDLStatsV6Json();
}

void SV_UploadMatchStats(int durationTics)
{
	std::string blob = SV_SerializeMatchStats();
	if (blob.empty())
		return;

	// The commit fires at match end, so "now" is the end of the match window and
	// the start is the recorded duration earlier.
	int durationSecs = durationTics / TICRATE;
	if (durationSecs < 1)
		durationSecs = 1;

	int64_t endedAt = static_cast<int64_t>(time(NULL));
	int64_t startedAt = endedAt - durationSecs;

	SV_ApiUploadMatchStats(blob, startedAt, endedAt);
}

VERSION_CONTROL (sv_stats_cpp, "$Id$")
