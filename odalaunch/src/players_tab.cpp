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
//   Players tab. See players_tab.h.
//
//-----------------------------------------------------------------------------

#include "players_tab.h"

#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/string.h>
#include <wx/xrc/xmlres.h>

#include "lst_custom.h"
#include "social_controller.h"

wxIMPLEMENT_DYNAMIC_CLASS(PlayersTab, wxPanel);

namespace
{
// Just the calendar date from an ISO-8601 UTC stamp ("2026-06-17T..."), or empty.
wxString DateOnly(const std::string& iso)
{
	if (iso.size() < 10)
		return wxEmptyString;
	return wxString::FromUTF8(iso.substr(0, 10));
}
} // namespace

PlayersTab::PlayersTab() = default;

void PlayersTab::PostInit()
{
	m_blocked = XRCCTRL(*this, "Id_BlockedList", wxAdvancedListCtrl);
	m_recent = XRCCTRL(*this, "Id_RecentPlayers", wxAdvancedListCtrl);
	m_community = XRCCTRL(*this, "Id_CommunityPlayers", wxAdvancedListCtrl);

	// The blocked/recent lists aren't sortable (HeaderUsable(false) keeps the
	// header labels but disables click-to-sort) so a clicked row maps straight to
	// its data.
	m_blocked->HeaderUsable(false);
	m_blocked->InsertColumn(0, "Player", wxLIST_FORMAT_LEFT, 180);
	m_blocked->InsertColumn(1, "Blocked since", wxLIST_FORMAT_LEFT, 110);
	m_blocked->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &PlayersTab::OnBlockedRightClick, this);

	m_recent->HeaderUsable(false);
	m_recent->InsertColumn(0, "Player", wxLIST_FORMAT_LEFT, 180);
	m_recent->InsertColumn(1, "Last played", wxLIST_FORMAT_LEFT, 110);

	// Display-only (no per-player actions); not sortable, matching the other two
	// lists (ALCInsertItem doesn't set up the click-sort metadata). Rows show in
	// server-query order.
	m_community->HeaderUsable(false);
	m_community->InsertColumn(0, "Player", wxLIST_FORMAT_LEFT, 160);
	m_community->InsertColumn(1, "Server", wxLIST_FORMAT_LEFT, 200);
	m_community->InsertColumn(2, "Ping", wxLIST_FORMAT_LEFT, 56);
}

void PlayersTab::SetController(SocialController* controller)
{
	m_controller = controller;
	if (!controller)
	{
		m_blocked->DeleteAllItems();
		m_blockedSubjects.clear();
		m_blockedSig.clear();
		m_recent->DeleteAllItems();
		return;
	}
	Refresh();
}

void PlayersTab::Refresh()
{
	if (!m_controller)
		return;
	const SocialState& state = m_controller->State();
	RebuildBlocked(state);
	RebuildRecent(state);
}

void PlayersTab::RebuildBlocked(const SocialState& state)
{
	// Skip the rebuild (which would drop selection) when nothing changed.
	std::string sig;
	for (const auto& b : state.blockedPlayers)
	{
		sig += b.subject;
		sig += '|';
		sig += b.username;
		sig += '\n';
	}
	if (sig == m_blockedSig)
		return;
	m_blockedSig = sig;

	m_blocked->DeleteAllItems();
	m_blockedSubjects.clear();

	for (const auto& b : state.blockedPlayers)
	{
		const wxString name = wxString::FromUTF8(b.username.empty() ? b.subject : b.username);
		const long row = m_blocked->ALCInsertItem(name);
		m_blocked->SetItem(row, 1, DateOnly(b.blockedSince));
		m_blockedSubjects.push_back(b.subject);
	}
}

void PlayersTab::RebuildRecent(const SocialState& WXUNUSED(state))
{
	// Placeholder until matchmaking lands and a "recently played with" feed
	// exists; for now show an empty-state hint so the section reads clearly.
	// Idempotent: Refresh() runs on every state change, so only seed it once.
	if (m_recent->GetItemCount() != 0)
		return;
	const long row = m_recent->ALCInsertItem("No recent matches yet.");
	m_recent->SetItem(row, 1, wxEmptyString);
}

void PlayersTab::SetCommunityPlayers(std::vector<CommunityPlayer> players)
{
	// Server-browser data, independent of the social controller: this is driven
	// by dlgMain on every server refresh and is unaffected by sign-in/out.
	if (!m_community)
		return;

	m_community->DeleteAllItems();
	for (const CommunityPlayer& p : players)
	{
		const long row = m_community->ALCInsertItem(p.name);
		m_community->SetItem(row, 1, p.server);
		m_community->SetItem(row, 2, wxString::Format("%d", p.ping));
	}
}

void PlayersTab::OnBlockedRightClick(wxListEvent& event)
{
	if (!m_controller)
		return;
	const long row = event.GetIndex();
	if (row < 0 || row >= static_cast<long>(m_blockedSubjects.size()))
		return;

	const int kUnblock = wxID_HIGHEST + 1;

	wxMenu menu;
	menu.Append(kUnblock, "Unblock");

	const int choice = GetPopupMenuSelectionFromUser(menu);
	if (choice == kUnblock)
		m_controller->Unblock(m_blockedSubjects[row]);
}
