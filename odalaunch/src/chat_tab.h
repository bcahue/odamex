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
//   Global-chat tab: a wxWebView scrollback (HTML/CSS — color emoji + good
//   typography on the Edge backend), an input row, and a native online-players
//   list. Renders from SocialState (owned by SocialController). Messages are
//   pushed into the page via RunScriptAsync and written with textContent, so
//   untrusted chat text can't inject markup; blocked authors collapse to a
//   click-to-reveal placeholder handled in JS. Timestamps are optional.
//
//-----------------------------------------------------------------------------

#pragma once

#include <wx/panel.h>

#include <string>
#include <vector>

class SocialController;
class SocialState;
class wxAdvancedListCtrl;
class wxWebView;
class wxWebViewEvent;
class wxTextCtrl;
class wxButton;
class wxCheckBox;
class wxStaticText;
class wxListEvent;

class ChatTab : public wxPanel
{
	wxDECLARE_DYNAMIC_CLASS(ChatTab);

  public:
	// Default-constructed by XRC (subclass="ChatTab"); the children are built
	// from XRC, then dlgMain calls PostInit() to wire everything up.
	ChatTab();

	// Retrieve the XRC-built child controls, create the web view, and bind
	// events. Call once, after the frame's XRC has loaded.
	void PostInit();

	// Set/clear the active controller on sign-in/out (null disables sending).
	void SetController(SocialController* controller);

	// Re-render the chat + players + status from the controller's SocialState.
	void Refresh();

  private:
	void SendCurrent();
	void OnSend(wxCommandEvent& event);
	void OnEnter(wxCommandEvent& event);
	void OnWebLoaded(wxWebViewEvent& event);
	void OnPlayerRightClick(wxListEvent& event); // Add Friend / Block-Unblock menu

	void RenderMessages(const SocialState& state);
	void RebuildPlayers(const SocialState& state);
	// Run a script in the page: async (non-blocking) on Edge, sync on the legacy
	// IE backend (which lacks RunScriptAsync). No-op until the page is ready.
	void RunJs(const wxString& script);
	// Local time string for an ISO-UTC stamp, or empty on parse failure.
	// use24Hour selects 24-hour vs 12-hour AM/PM; showSeconds adds seconds.
	wxString FormatTimestamp(const std::string& isoUtc, bool use24Hour,
	                         bool showSeconds) const;

	SocialController* m_controller = nullptr;

	wxWebView* m_web = nullptr;
	bool m_webReady = false; // SetPage is async; only script once the page is loaded
	bool m_isEdge = false;   // Edge backend in use -> RunScriptAsync; else RunScript
	wxTextCtrl* m_input = nullptr;
	wxButton* m_send = nullptr;
	wxStaticText* m_status = nullptr;
	wxAdvancedListCtrl* m_players = nullptr;

	// Subjects of the online-players list, in row order (the list is not
	// sortable, so a clicked row index maps straight to a subject here).
	std::vector<std::string> m_playerSubjects;
	std::string m_playersSig;   // skip needless list rebuilds when unchanged
	std::string m_selfSubject;  // the caller's own subject (no context menu on it)
};
