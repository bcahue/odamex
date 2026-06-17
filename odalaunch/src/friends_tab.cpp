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
//   Friends tab. See friends_tab.h.
//
//-----------------------------------------------------------------------------

#include "friends_tab.h"

#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/string.h>
#include <wx/xrc/xmlres.h>

#include "lst_custom.h"
#include "social_controller.h"

wxIMPLEMENT_DYNAMIC_CLASS(FriendsTab, wxPanel);

namespace
{
// Friendly presence label for a status string ("Online" | "Offline" | "InMatch").
wxString StatusLabel(const std::string& status)
{
	if (status == "InMatch")
		return "In match";
	return wxString::FromUTF8(status);
}
} // namespace

FriendsTab::FriendsTab() = default;

void FriendsTab::PostInit()
{
	m_friends = XRCCTRL(*this, "Id_FriendsList", wxAdvancedListCtrl);
	m_requests = XRCCTRL(*this, "Id_FriendRequests", wxAdvancedListCtrl);

	// Neither list is sortable (HeaderUsable(false) keeps the header labels but
	// disables click-to-sort), so a clicked row maps straight to its data.
	m_friends->HeaderUsable(false);
	m_friends->InsertColumn(0, "Friend", wxLIST_FORMAT_LEFT, 180);
	m_friends->InsertColumn(1, "Status", wxLIST_FORMAT_LEFT, 90);
	m_friends->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &FriendsTab::OnFriendRightClick, this);

	m_requests->HeaderUsable(false);
	m_requests->InsertColumn(0, "User", wxLIST_FORMAT_LEFT, 180);
	m_requests->InsertColumn(1, "Direction", wxLIST_FORMAT_LEFT, 90);
	m_requests->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &FriendsTab::OnRequestRightClick, this);
}

void FriendsTab::SetController(SocialController* controller)
{
	m_controller = controller;
	if (!controller)
	{
		m_friends->DeleteAllItems();
		m_friendSubjects.clear();
		m_friendsSig.clear();
		m_requests->DeleteAllItems();
		m_requestRows.clear();
		m_requestsSig.clear();
		return;
	}
	Refresh();
}

void FriendsTab::Refresh()
{
	if (!m_controller)
		return;
	const SocialState& state = m_controller->State();
	RebuildFriends(state);
	RebuildRequests(state);
}

void FriendsTab::RebuildFriends(const SocialState& state)
{
	// Skip the rebuild (which would drop selection) when nothing changed.
	std::string sig;
	for (const auto& f : state.friends)
	{
		sig += f.subject;
		sig += '|';
		sig += f.status;
		sig += '\n';
	}
	if (sig == m_friendsSig)
		return;
	m_friendsSig = sig;

	m_friends->DeleteAllItems();
	m_friendSubjects.clear();

	for (const auto& f : state.friends)
	{
		const wxString name =
		    wxString::FromUTF8(f.username.empty() ? f.subject : f.username);
		const long row = m_friends->ALCInsertItem(name);
		m_friends->SetItem(row, 1, StatusLabel(f.status));
		m_friendSubjects.push_back(f.subject);
	}
}

void FriendsTab::RebuildRequests(const SocialState& state)
{
	// incoming first, then outgoing; signature guards against needless rebuilds.
	std::string sig;
	for (const auto& r : state.incoming)
	{
		sig += "i";
		sig += std::to_string(r.requestId);
		sig += '|';
		sig += r.fromUsername;
		sig += '\n';
	}
	for (const auto& r : state.outgoing)
	{
		sig += "o";
		sig += std::to_string(r.requestId);
		sig += '|';
		sig += r.toUsername;
		sig += '\n';
	}
	if (sig == m_requestsSig)
		return;
	m_requestsSig = sig;

	m_requests->DeleteAllItems();
	m_requestRows.clear();

	for (const auto& r : state.incoming)
	{
		const wxString name =
		    wxString::FromUTF8(r.fromUsername.empty() ? r.fromSubject : r.fromUsername);
		const long row = m_requests->ALCInsertItem(name);
		m_requests->SetItem(row, 1, "Incoming");
		m_requestRows.emplace_back(r.requestId, true);
	}
	for (const auto& r : state.outgoing)
	{
		const wxString name =
		    wxString::FromUTF8(r.toUsername.empty() ? r.toSubject : r.toUsername);
		const long row = m_requests->ALCInsertItem(name);
		m_requests->SetItem(row, 1, "Outgoing");
		m_requestRows.emplace_back(r.requestId, false);
	}
}

void FriendsTab::OnFriendRightClick(wxListEvent& event)
{
	if (!m_controller)
		return;
	const long row = event.GetIndex();
	if (row < 0 || row >= static_cast<long>(m_friendSubjects.size()))
		return;
	const std::string sub = m_friendSubjects[row];

	const int kRemove = wxID_HIGHEST + 1;
	const int kBlock = wxID_HIGHEST + 2;

	wxMenu menu;
	menu.Append(kRemove, "Remove Friend");
	menu.Append(kBlock, "Block");

	const int choice = GetPopupMenuSelectionFromUser(menu);
	if (choice == kRemove)
		m_controller->RemoveFriend(sub);
	else if (choice == kBlock)
		m_controller->Block(sub); // server cascade also drops the friendship
}

void FriendsTab::OnRequestRightClick(wxListEvent& event)
{
	if (!m_controller)
		return;
	const long row = event.GetIndex();
	if (row < 0 || row >= static_cast<long>(m_requestRows.size()))
		return;
	const long long id = m_requestRows[row].first;
	const bool incoming = m_requestRows[row].second;

	const int kAccept = wxID_HIGHEST + 1;
	const int kDecline = wxID_HIGHEST + 2;
	const int kCancel = wxID_HIGHEST + 3;

	wxMenu menu;
	if (incoming)
	{
		menu.Append(kAccept, "Accept");
		menu.Append(kDecline, "Decline");
	}
	else
	{
		menu.Append(kCancel, "Cancel Request");
	}

	const int choice = GetPopupMenuSelectionFromUser(menu);
	if (choice == kAccept)
		m_controller->AcceptRequest(id);
	else if (choice == kDecline)
		m_controller->DeclineRequest(id);
	else if (choice == kCancel)
		m_controller->CancelRequest(id);
}
