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

#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/xrc/xmlres.h>

#include "lst_custom.h"
#include "social_controller.h"

wxIMPLEMENT_DYNAMIC_CLASS(PartyTab, wxPanel);

PartyTab::PartyTab() = default;

void PartyTab::PostInit()
{
	m_status = XRCCTRL(*this, "Id_PartyStatus", wxStaticText);

	m_members = XRCCTRL(*this, "Id_PartyMembers", wxAdvancedListCtrl);
	m_members->HeaderUsable(false);
	m_members->InsertColumn(0, "Member", wxLIST_FORMAT_LEFT, 200);
	m_members->InsertColumn(1, "Role", wxLIST_FORMAT_LEFT, 90);
	m_members->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &PartyTab::OnMemberRightClick, this);

	m_invites = XRCCTRL(*this, "Id_PartyInvites", wxAdvancedListCtrl);
	m_invites->HeaderUsable(false);
	m_invites->InsertColumn(0, "Invited by", wxLIST_FORMAT_LEFT, 290);
	m_invites->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &PartyTab::OnInviteRightClick, this);
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
		if (m_status)
			m_status->SetLabel(wxEmptyString);
		return;
	}
	Refresh();
}

void PartyTab::Refresh()
{
	if (!m_controller)
		return;
	const SocialState& state = m_controller->State();
	UpdateStatus(state);
	RebuildMembers(state);
	RebuildInvites(state);
}

void PartyTab::UpdateStatus(const SocialState& state)
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
