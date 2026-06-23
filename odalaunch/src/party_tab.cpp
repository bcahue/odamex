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
//   Party tab. See party_tab.h.
//
//-----------------------------------------------------------------------------

#include "party_tab.h"

#include <wx/button.h>
#include <wx/fileconf.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/textctrl.h>
#include <wx/utils.h>
#include <wx/xrc/xmlres.h>

#include <string>

#include "lst_custom.h"
#include "oda_defs.h"
#include "profanity_filter.h"
#include "social_controller.h"

wxIMPLEMENT_DYNAMIC_CLASS(PartyTab, wxPanel);

PartyTab::PartyTab() = default;

void PartyTab::PostInit()
{
	m_status = XRCCTRL(*this, "Id_PartyStatus", wxStaticText);
	m_lobbySelection = XRCCTRL(*this, "Id_PartyLobbySelection", wxStaticText);

	m_members = XRCCTRL(*this, "Id_PartyMembers", wxAdvancedListCtrl);
	m_members->HeaderUsable(false);
	m_members->InsertColumn(0, "Member", wxLIST_FORMAT_LEFT, 200);
	m_members->InsertColumn(1, "Role", wxLIST_FORMAT_LEFT, 90);
	m_members->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &PartyTab::OnMemberRightClick, this);

	m_invites = XRCCTRL(*this, "Id_PartyInvites", wxAdvancedListCtrl);
	m_invites->HeaderUsable(false);
	m_invites->InsertColumn(0, "Invited by", wxLIST_FORMAT_LEFT, 290);
	m_invites->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &PartyTab::OnInviteRightClick, this);

	wxPanel* chatHost = XRCCTRL(*this, "Id_PartyChatHost", wxPanel);
	m_chat.OnOpenUrl = [](const wxString& url) { wxLaunchDefaultBrowser(url); };
	m_chat.OnUserMenu = [this](const std::string& sub) { ShowUserContextMenu(sub); };
	m_chat.OnReady = [this]() {
		if (m_controller)
			RenderChat(m_controller->State());
	};
	m_chat.Create(chatHost);

	m_chatInput = XRCCTRL(*this, "Id_PartyChatInput", wxTextCtrl);
	m_sendButton = XRCCTRL(*this, "Id_PartyChatSendBtn", wxButton);
	m_leaveButton = XRCCTRL(*this, "Id_PartyLeaveBtn", wxButton);

	m_splitMain = XRCCTRL(*this, "Id_PartySplitMain", wxSplitterWindow);
	m_splitChat = XRCCTRL(*this, "Id_PartySplitChat", wxSplitterWindow);
	Bind(wxEVT_SIZE, &PartyTab::OnSize, this);

	m_sendButton->Bind(wxEVT_BUTTON, &PartyTab::OnSend, this);
	m_chatInput->Bind(wxEVT_TEXT_ENTER, &PartyTab::OnEnter, this);
	m_leaveButton->Bind(wxEVT_BUTTON, &PartyTab::OnLeave, this);

	m_chatInput->Enable(false);
	m_sendButton->Enable(false);
	m_leaveButton->Enable(false);
}

void PartyTab::SetController(SocialController* controller)
{
	m_controller = controller;
	if (!controller)
	{
		m_members->DeleteAllItems();
		m_memberSubjects.clear();
		m_membersSig.clear();
		m_invites->DeleteAllItems();
		m_inviteIds.clear();
		m_invitesSig.clear();
		m_chat.SetMessages("[]");
		m_chatSig.clear();
		m_chatInput->Clear();
		m_chatInput->Enable(false);
		m_sendButton->Enable(false);
		m_leaveButton->Enable(false);
		if (m_status)
			m_status->SetLabel(wxEmptyString);
		if (m_lobbySelection)
			m_lobbySelection->SetLabel(wxEmptyString);
		return;
	}
	Refresh();
}

void PartyTab::Refresh()
{
	if (!m_controller)
		return;
	const SocialState& state = m_controller->State();
	UpdateStatusAndButtons(state);
	UpdateLobbySelection(state);
	RebuildMembers(state);
	RebuildInvites(state);
	RenderChat(state);
}

void PartyTab::UpdateLobbySelection(const SocialState& state)
{
	if (!m_lobbySelection)
		return;

	// The party hub snapshot doesn't carry a lobby selection yet (the read-only
	// LobbySelection mirror is still to come), so show placeholder guidance.
	wxString text;
	if (!state.party.active)
		text = wxEmptyString;
	else if (state.party.selfIsLeader)
		text = "Pick a lobby from the Matchmaking or Quickplay tab to share it with your party.";
	else
		text = "The party leader hasn't selected a lobby yet.";

	m_lobbySelection->SetLabel(text);
	m_lobbySelection->Wrap(m_lobbySelection->GetClientSize().GetWidth());
}

void PartyTab::UpdateStatusAndButtons(const SocialState& state)
{
	const SocialParty& party = state.party;

	wxString status;
	if (!party.active)
	{
		status = "You're not in a party. Invite a friend or player to start one.";
	}
	else
	{
		status = wxString::Format("Party of %d - %s", (int)party.members.size(),
		                          party.selfIsLeader ? "you are the leader"
		                                             : "you are a member");
	}
	if (m_status)
		m_status->SetLabel(status);

	// Chat + leave are available whenever you're actually in a party.
	m_chatInput->Enable(party.active);
	m_sendButton->Enable(party.active);
	m_leaveButton->Enable(party.active);
}

void PartyTab::RebuildMembers(const SocialState& state)
{
	const SocialParty& party = state.party;

	std::string sig;
	for (const auto& m : party.members)
	{
		sig += m.subject;
		sig += m.isLeader ? "|L" : "|.";
		sig += m.isSelf ? "|S" : "|.";
		sig += '|';
		sig += m.username;
		sig += '\n';
	}
	if (sig == m_membersSig)
		return;
	m_membersSig = sig;

	m_members->DeleteAllItems();
	m_memberSubjects.clear();

	for (const auto& m : party.members)
	{
		wxString name = wxString::FromUTF8(m.username.empty() ? m.subject : m.username);
		if (m.isSelf)
			name += " (you)";
		const long row = m_members->ALCInsertItem(name);
		m_members->SetItem(row, 1, m.isLeader ? "Leader" : "Member");
		m_memberSubjects.push_back(m.subject);
	}
}

void PartyTab::RebuildInvites(const SocialState& state)
{
	std::string sig;
	for (const auto& inv : state.partyInvites)
	{
		sig += inv.inviteId;
		sig += '|';
		sig += inv.inviterUsername;
		sig += '\n';
	}
	if (sig == m_invitesSig)
		return;
	m_invitesSig = sig;

	m_invites->DeleteAllItems();
	m_inviteIds.clear();

	for (const auto& inv : state.partyInvites)
	{
		const wxString name = wxString::FromUTF8(
		    inv.inviterUsername.empty() ? inv.inviterSubject : inv.inviterUsername);
		m_invites->ALCInsertItem(name);
		m_inviteIds.push_back(inv.inviteId);
	}
}

void PartyTab::RenderChat(const SocialState& state)
{
	std::string sig;
	for (const auto& line : state.party.chat)
	{
		sig += line.sentAt;
		sig += '|';
		sig += line.fromSubject;
		sig += '|';
		sig += line.text;
		sig += '\n';
	}
	if (sig == m_chatSig)
		return;
	m_chatSig = sig;

	// Match the global-chat tab's profanity preference (default on).
	bool filterProfanity = true;
	{
		wxFileConfig cfg;
		cfg.Read(FILTERPROFANITY, &filterProfanity, ODA_UIFILTERPROFANITY);
	}

	// Build the same setMessages() payload the global-chat tab uses. Party chat
	// has no per-message id and is never "blocked", so the id is just a render
	// key (the row index) and ts is left empty (no timestamps in party chat).
	std::string json = "[";
	bool first = true;
	int idx = 0;
	for (const auto& line : state.party.chat)
	{
		if (!first)
			json += ",";
		first = false;

		const std::string user = line.fromUsername.empty() ? line.fromSubject : line.fromUsername;
		const std::string text = filterProfanity ? CensorProfanity(line.text) : line.text;

		json += "{\"id\":" + ChatWebView::JsonStr(std::to_string(idx++));
		json += ",\"user\":" + ChatWebView::JsonStr(user);
		json += ",\"sub\":" + ChatWebView::JsonStr(line.fromSubject);
		json += ",\"text\":" + ChatWebView::JsonStr(text);
		json += ",\"ts\":\"\"";
		json += ",\"blocked\":false";
		json += ",\"deleted\":false";
		json += "}";
	}
	json += "]";

	m_chat.SetMessages(wxString::FromUTF8(json));
}

void PartyTab::ShowUserContextMenu(const std::string& sub)
{
	if (!m_controller || sub.empty())
		return;

	const SocialState& state = m_controller->State();
	// No friend/block actions on yourself.
	if (sub == state.selfSubject)
		return;

	const bool blocked = state.IsBlocked(sub);

	const int kAddFriend = wxID_HIGHEST + 1;
	const int kBlock = wxID_HIGHEST + 2;
	const int kUnblock = wxID_HIGHEST + 3;

	wxMenu menu;
	if (blocked)
	{
		menu.Append(kUnblock, "Unblock");
	}
	else
	{
		menu.Append(kAddFriend, "Add Friend");
		menu.Append(kBlock, "Block");
	}

	const int choice = GetPopupMenuSelectionFromUser(menu);
	if (choice == kAddFriend)
		m_controller->SendFriendRequest(sub);
	else if (choice == kBlock)
		m_controller->Block(sub);
	else if (choice == kUnblock)
		m_controller->Unblock(sub);
}

void PartyTab::OnMemberRightClick(wxListEvent& event)
{
	if (!m_controller)
		return;
	const SocialState& state = m_controller->State();
	// Leader-only actions; nothing to do for a non-leader.
	if (!state.party.selfIsLeader)
		return;

	const long row = event.GetIndex();
	if (row < 0 || row >= static_cast<long>(m_memberSubjects.size()))
		return;
	const std::string sub = m_memberSubjects[row];

	// No actions against yourself.
	if (sub == state.selfSubject)
		return;

	const int kPromote = wxID_HIGHEST + 1;
	const int kKick = wxID_HIGHEST + 2;

	wxMenu menu;
	menu.Append(kPromote, "Promote to Leader");
	menu.Append(kKick, "Kick from Party");

	const int choice = GetPopupMenuSelectionFromUser(menu);
	if (choice == kPromote)
		m_controller->PromoteToLeader(sub);
	else if (choice == kKick)
		m_controller->KickFromParty(sub);
}

void PartyTab::OnInviteRightClick(wxListEvent& event)
{
	if (!m_controller)
		return;
	const long row = event.GetIndex();
	if (row < 0 || row >= static_cast<long>(m_inviteIds.size()))
		return;
	const std::string inviteId = m_inviteIds[row];

	const int kAccept = wxID_HIGHEST + 1;
	const int kDecline = wxID_HIGHEST + 2;

	wxMenu menu;
	menu.Append(kAccept, "Accept");
	menu.Append(kDecline, "Decline");

	const int choice = GetPopupMenuSelectionFromUser(menu);
	if (choice == kAccept)
		m_controller->AcceptPartyInvite(inviteId);
	else if (choice == kDecline)
		m_controller->DeclinePartyInvite(inviteId);
}

void PartyTab::OnSend(wxCommandEvent& WXUNUSED(event))
{
	SendCurrent();
}

void PartyTab::OnEnter(wxCommandEvent& WXUNUSED(event))
{
	SendCurrent();
}

void PartyTab::OnLeave(wxCommandEvent& WXUNUSED(event))
{
	if (m_controller)
		m_controller->LeaveParty();
}

void PartyTab::OnSize(wxSizeEvent& event)
{
	event.Skip(); // let the panel lay out normally

	if (m_sashInit || !m_splitMain || !m_splitChat)
		return;

	const int h = m_splitMain->GetClientSize().GetHeight();
	const int w = m_splitChat->GetClientSize().GetWidth();
	if (h < 60 || w < 60)
		return; // not laid out yet; try again on the next size event

	// Top third for invites + selection; chat gets two-thirds of the bottom.
	m_splitMain->SetSashPosition(h / 3);
	m_splitChat->SetSashPosition(w * 2 / 3);
	m_sashInit = true;
}

void PartyTab::SendCurrent()
{
	if (!m_controller)
		return;
	const wxString text = m_chatInput->GetValue();
	if (text.empty())
		return;
	m_controller->SendPartyMessage(text.utf8_string());
	m_chatInput->Clear();
}
