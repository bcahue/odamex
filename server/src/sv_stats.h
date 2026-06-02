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
// B7 SCAFFOLD — match-stats serialization + upload.
//
// This module turns the in-memory results of a finished match into the JSON
// blob the API expects and hands it to the async API client for upload. The v1
// schema is deliberately loose (an opaque blob), but the exact shape still needs
// a design pass, so the bodies below are stubs. See the v1 shipping plan, B7.
// ===========================================================================

// Serialize the current/just-finished match into the API's stats JSON blob.
// TODO(B7): walk the existing match-end results and emit the agreed schema.
// Returns an empty string until implemented.
std::string SV_SerializeMatchStats();

// Entry point to call at match end: serialize, then hand off to the async
// uploader (SV_ApiUploadMatchStats). Currently inert.
// TODO(B7): invoke from the match-end / intermission path once the serializer
// and the API contract are settled.
void SV_UploadMatchStats();

