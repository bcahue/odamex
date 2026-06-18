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
//   Players tab: lets the player review the people they block (with an Unblock
//   action) and, once matchmaking lands, the players they were recently in a
//   match with. Two native wxAdvancedListCtrls rendering from SocialState and
//   acting through SocialController. XRC-subclassed like FriendsTab/ChatTab.
//   The "recently played with" list is a scaffolded placeholder for now.
//
//-----------------------------------------------------------------------------

#pragma once

#include <wx/panel.h>
#include <wx/string.h>

#include <string>
#include <vector>

class SocialController;
class SocialState;
class wxAdvancedListCtrl;
class wxListEvent;

class PlayersTab : public wxPanel
{
	wxDECLARE_DYNAMIC_CLASS(PlayersTab);

  public:
	// Default-constructed by XRC (subclass="PlayersTab"); dlgMain calls PostInit().
	PlayersTab();

	// Retrieve the XRC-built child controls and bind events. Call once.
	void PostInit();

	// Set/clear the active controller on sign-in/out (null clears the lists).
	void SetController(SocialController* controller);

	// Re-render the blocked / recently-played lists from the SocialState.
	void Refresh();

	// One player found on a community server during a server-browser refresh.
	// These are in-game identities (no account), so the list is display-only.
	struct CommunityPlayer
	{
		wxString name;
		wxString server;
		int ping = 0;
	};

	// Replace the "community server players" list. Driven by the server browser
	// (dlgMain) on refresh, independent of the social sign-in lifecycle.
	void SetCommunityPlayers(std::vector<CommunityPlayer> players);

  private:
	void OnBlockedRightClick(wxListEvent& event); // Unblock

	void RebuildBlocked(const SocialState& state);
	void RebuildRecent(const SocialState& state);

	SocialController* m_controller = nullptr;

	wxAdvancedListCtrl* m_blocked = nullptr;
	wxAdvancedListCtrl* m_recent = nullptr;
	wxAdvancedListCtrl* m_community = nullptr;

	// Row-order subjects for the blocked list (not sortable, so a clicked row
	// maps straight to a subject here). A signature skips needless rebuilds.
	std::vector<std::string> m_blockedSubjects;
	std::string m_blockedSig;
};
