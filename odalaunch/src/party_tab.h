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
//   Party tab: the current party's member list (with a leader badge and
//   leader-only kick / promote), pending incoming invites (Accept / Decline),
//   and party chat. Renders from SocialState and acts through SocialController,
//   XRC-subclassed like FriendsTab / ChatTab. The always-available social tab;
//   leader status here gates the (later) Matchmaking / Quickplay tabs.
//
//-----------------------------------------------------------------------------

#pragma once

#include <wx/panel.h>

#include <string>
#include <vector>

#include "chat_webview.h"

class SocialController;
class SocialState;
class wxAdvancedListCtrl;
class wxButton;
class wxCommandEvent;
class wxListEvent;
class wxSizeEvent;
class wxSplitterWindow;
class wxStaticText;
class wxTextCtrl;

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

	// Re-render members + invites + chat from the controller's SocialState.
	void Refresh();

  private:
	void OnMemberRightClick(wxListEvent& event);  // leader: Promote / Kick
	void OnInviteRightClick(wxListEvent& event);  // Accept / Decline
	void OnSend(wxCommandEvent& event);
	void OnEnter(wxCommandEvent& event);
	void OnLeave(wxCommandEvent& event);
	// Seed the two splitter sashes to 1/3 (top/bottom) and 2/3 (chat/members) on
	// the first real layout, then leave them to the user / sash gravity.
	void OnSize(wxSizeEvent& event);
	void SendCurrent();

	void RebuildMembers(const SocialState& state);
	void RebuildInvites(const SocialState& state);
	void RenderChat(const SocialState& state);
	void UpdateStatusAndButtons(const SocialState& state);
	// Placeholder text for the leader-selection panel until the LobbySelection
	// mirror is wired (the party hub snapshot doesn't carry a selection yet).
	void UpdateLobbySelection(const SocialState& state);
	// Add Friend / Block(/Unblock) menu for a chat username's subject, shown when
	// a name is right-clicked in the shared chat scrollback.
	void ShowUserContextMenu(const std::string& subject);

	SocialController* m_controller = nullptr;

	wxStaticText* m_status = nullptr;
	wxStaticText* m_lobbySelection = nullptr; // what the party leader has selected
	wxSplitterWindow* m_splitMain = nullptr;  // top (invites+selection) / bottom (chat+members)
	wxSplitterWindow* m_splitChat = nullptr;  // chat / members
	bool m_sashInit = false;                  // sashes seeded to thirds yet?
	wxAdvancedListCtrl* m_members = nullptr;
	wxAdvancedListCtrl* m_invites = nullptr;
	ChatWebView m_chat;      // HTML scrollback (shared with the global-chat tab)
	wxTextCtrl* m_chatInput = nullptr;
	wxButton* m_sendButton = nullptr;
	wxButton* m_leaveButton = nullptr;

	// Row-order subject for the members list + a signature to skip needless rebuilds.
	std::vector<std::string> m_memberSubjects;
	std::string m_membersSig;

	// Row-order invite id for the invites list + a signature.
	std::vector<std::string> m_inviteIds;
	std::string m_invitesSig;

	// Chat scrollback signature so we only re-render when it changes.
	std::string m_chatSig;
};
