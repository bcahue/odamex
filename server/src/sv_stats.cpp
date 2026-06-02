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


#include "odamex.h"

#include "sv_stats.h"

#include "sv_apiclient.h"

// ===========================================================================
// B7 SCAFFOLD — see sv_stats.h. These are intentionally inert placeholders so
// the call shape exists for the future B7 implementation without changing any
// current behavior.
// ===========================================================================

std::string SV_SerializeMatchStats()
{
	// TODO(B7): convert the in-memory match results into the API's stats JSON.
	// The schema is an opaque blob in v1; wrap whatever the existing match-end
	// bookkeeping produces (per-player frags/deaths/etc., map, duration, the
	// server_id, and the participating players' subjects).
	return std::string();
}

void SV_UploadMatchStats()
{
	// TODO(B7): wire this into the match-end / intermission path. For now it is
	// not called from anywhere, so producing/uploading stats is a no-op.
	std::string blob = SV_SerializeMatchStats();
	if (blob.empty())
		return;

	SV_ApiUploadMatchStats(blob);
}

VERSION_CONTROL (sv_stats_cpp, "$Id$")
