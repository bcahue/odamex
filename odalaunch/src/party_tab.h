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
//   Party tab (a page of the social side-panel notebook): the current party's
//   member list (with a leader badge and leader-only kick / promote), pending
//   incoming invites (Accept / Decline), and a Leave Party button shown only
//   while in a party. Party chat lives separately in PartyChatPanel. Renders
//   from SocialState and acts through SocialController, XRC-subclassed.
//
//-----------------------------------------------------------------------------

#pragma once

#include <wx/panel.h>

#include <string>
#include <vector>

class SocialController;
class SocialState;
class wxAdvancedListCtrl;
class wxListEvent;
class wxStaticText;

class PartyTab : public wxPanel
{
	wxDECLARE_DYNAMIC_CLASS(PartyTab);

  public:
	// Default-constructed by XRC (subclass="PartyTab"); dlgMain calls PostInit().
	PartyTab();

	// Retrieve the XRC-built child controls and bind events. Call once.
	void PostInit();

	// Set/clear the active controller on sign-in/out (null clears the views).
	void SetController(SocialController* controller);

	// Re-render members + invites + status from the controller's SocialState.
	void Refresh();

  private:
	void OnMemberRightClick(wxListEvent& event); // leader: Promote / Kick
	void OnInviteRightClick(wxListEvent& event); // Accept / Decline

	void RebuildMembers(const SocialState& state);
	void RebuildInvites(const SocialState& state);
	void UpdateStatus(const SocialState& state);

	SocialController* m_controller = nullptr;

	wxStaticText* m_status = nullptr;
	wxAdvancedListCtrl* m_members = nullptr;
	wxAdvancedListCtrl* m_invites = nullptr;

	// Row-order subject for the members list + a signature to skip needless rebuilds.
	std::vector<std::string> m_memberSubjects;
	std::string m_membersSig;

	// Row-order invite id for the invites list + a signature.
	std::vector<std::string> m_inviteIds;
	std::string m_invitesSig;
};
