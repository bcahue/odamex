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
//   First-time registration completion for the launcher. When a brand-new
//   account logs in, the API's callback bounces back with a short-lived
//   `pending` token instead of a session token (see LauncherLogin /
//   LauncherLoginResult::PendingRegistration). The user must then pick a
//   username; this module drives the two endpoints that finish the job:
//     GET  /api/launcher/auth/username-available  (cheap availability check)
//     POST /api/launcher/auth/complete-registration (commit username -> session)
//   Neither endpoint is DPoP/Bearer protected -- the pending token is the
//   credential, and the resulting session is bound to the DPoP key captured at
//   /start time.
//
//-----------------------------------------------------------------------------

#pragma once

#include <wx/string.h>

namespace LauncherRegistration
{
// Result of a username availability pre-check.
struct UsernameAvailability
{
	enum class Status
	{
		Available, // free to use
		Taken,     // already in use
		Invalid,   // fails the server's format rules
		Error      // network/other failure (treat as "couldn't check")
	};
	Status status = Status::Error;
};

// Cheap, optional pre-check for live UI feedback as the user types. Does not
// reserve the name; the real commit is Complete().
UsernameAvailability CheckUsername(const wxString& apiBaseUrl,
                                   const wxString& username);

// Result of committing a username for the pending registration.
struct RegistrationResult
{
	enum class Status
	{
		Success,         // registered; sessionToken/username populated
		UsernameTaken,   // retry with a fresh pending token (newPendingToken)
		InvalidUsername, // bad format; user picks again (same pending token)
		Expired,         // pending token expired/unknown; must restart login
		Failed           // network/other failure
	};

	Status status = Status::Failed;
	wxString sessionToken;    // Success
	wxString username;        // Success
	wxString newPendingToken; // UsernameTaken
	wxString error;           // diagnostic detail
};

// Commit the chosen username. On UsernameTaken the API returns a *new* pending
// token (the old one is consumed), so the caller should loop using
// newPendingToken until Success or a terminal status.
RegistrationResult Complete(const wxString& apiBaseUrl,
                            const wxString& pendingToken,
                            const wxString& username);
} // namespace LauncherRegistration
