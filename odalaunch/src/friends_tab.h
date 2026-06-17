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
//   Friends tab (slice L5): a list of friends with presence, and a list of
//   pending requests (incoming = Accept/Decline, outgoing = Cancel). Both are
//   native wxAdvancedListCtrls with right-click context menus, rendering from
//   SocialState and acting through SocialController. XRC-subclassed like ChatTab.
//
//-----------------------------------------------------------------------------

#pragma once

#include <wx/panel.h>

#include <string>
#include <utility>
#include <vector>

class SocialController;
class SocialState;
class wxAdvancedListCtrl;
class wxButton;
class wxCommandEvent;
class wxListEvent;
class wxStaticText;
class wxTextCtrl;

class FriendsTab : public wxPanel
{
	wxDECLARE_DYNAMIC_CLASS(FriendsTab);

  public:
	// Default-constructed by XRC (subclass="FriendsTab"); dlgMain calls PostInit().
	FriendsTab();

	// Retrieve the XRC-built child controls and bind events. Call once.
	void PostInit();

	// Set/clear the active controller on sign-in/out (null clears the lists).
	void SetController(SocialController* controller);

	// Re-render friends + requests from the controller's SocialState.
	void Refresh();

  private:
	void OnFriendRightClick(wxListEvent& event);  // Remove friend / Block
	void OnRequestRightClick(wxListEvent& event); // Accept-Decline / Cancel
	void OnAddFriend(wxCommandEvent& event);      // Add button
	void OnAddFriendEnter(wxCommandEvent& event); // Enter in the username box
	void SubmitAddFriend();

	void RebuildFriends(const SocialState& state);
	void RebuildRequests(const SocialState& state);

	SocialController* m_controller = nullptr;

	wxAdvancedListCtrl* m_friends = nullptr;
	wxAdvancedListCtrl* m_requests = nullptr;
	wxTextCtrl* m_addInput = nullptr;
	wxButton* m_addButton = nullptr;
	wxStaticText* m_addStatus = nullptr;

	// Row-order subject for the friends list (not sortable, so a clicked row
	// maps straight to a subject here). A signature skips needless rebuilds.
	std::vector<std::string> m_friendSubjects;
	std::string m_friendsSig;

	// Row-order (requestId, isIncoming) for the requests list, + a signature.
	std::vector<std::pair<long long, bool>> m_requestRows;
	std::string m_requestsSig;
};
