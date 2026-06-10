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
//   WDLStats v6 aggregation — dispatch loop + event ingestion (chunk S4a).
//   See m_wdlstats_agg.h for the chunk breakdown.
//
//-----------------------------------------------------------------------------

#include "odamex.h"

#include "m_wdlstats_agg.h"

// ---------------------------------------------------------------------------
// WDLAggPlayer
// ---------------------------------------------------------------------------

WDLAggPlayer::WDLAggPlayer(const WDLPlayer& src)
    : id(src.id), netid(src.pid), name(src.netname), team(src.team), sub(src.sub)
{
}

// The handler bodies below are intentionally empty in S4a: this chunk wires the
// dispatch so each event reaches the right handler with faithfully mapped
// fields and units. The per-player simulation and stat capture land in S4b–S4d.

bool WDLAggPlayer::PlayerHasFlag() const
{
	return false; // TODO(S4d): flag-possession tracking
}

WDLArmorV6 WDLAggPlayer::GetArmorType() const
{
	return WDLArmorV6::None; // TODO(S4b): armor-type simulation
}

void WDLAggPlayer::TakeDamageFromPlayer(int /*health*/, int /*armor*/, WDLArmorV6 /*armorType*/)
{
	// TODO(S4b): health/armor simulation (the replay model from §3).
}

void WDLAggPlayer::AwardDamageToPlayer(int /*damagedId*/, const std::string& /*damagedName*/,
                                       int /*hp*/, int /*armor*/, WDLArmorV6 /*armorType*/,
                                       WDLDamageTypeV6 /*damageType*/, int /*mod*/,
                                       bool /*selfHasFlag*/, bool /*targetHasFlag*/,
                                       int /*ticsElapsed*/, int /*ax*/, int /*ay*/, int /*az*/,
                                       int /*tx*/, int /*ty*/, int /*tz*/)
{
	// TODO(S4b): append to the appropriate damage list(s).
}

void WDLAggPlayer::AwardKill(bool /*playerKilledHadFlag*/, bool /*playerHadFlag*/,
                             bool /*isTeamKill*/, int /*mod*/, int /*ticsElapsed*/,
                             int /*killedId*/, const std::string& /*killedName*/, int /*tx*/,
                             int /*ty*/, int /*tz*/, int /*ax*/, int /*ay*/, int /*az*/)
{
	// TODO(S4b): kill lists + spree/multi-kill windows.
}

void WDLAggPlayer::PlayerKilled(int /*ticsElapsed*/, WDLDeathTypeV6 /*deathType*/,
                                int /*weaponMod*/, int /*x*/, int /*y*/, int /*z*/)
{
	// TODO(S4b): death counters; reset spree state.
}

void WDLAggPlayer::PlayerTouchedFlag(WDLFlagTouchTypeV6 /*touchType*/, int /*ticsElapsed*/,
                                     int /*ax*/, int /*ay*/, int /*az*/)
{
	// TODO(S4d): flag-touch list + flag possession.
}

void WDLAggPlayer::AwardFlagCapture(int /*ticsElapsed*/, bool /*isPickupCapture*/, int /*fx*/,
                                    int /*fy*/, int /*fz*/)
{
	// TODO(S4d): capture list + touch→capture correlation.
}

void WDLAggPlayer::AwardFlagReturn(int /*ax*/, int /*ay*/, int /*az*/, int /*ticsElapsed*/)
{
	// TODO(S4d): flag-return events + counter.
}

void WDLAggPlayer::AwardPickup(int /*pickupType*/, int /*itemId*/, bool /*dropped*/,
                               int /*ticsElapsed*/)
{
	// TODO(S4b): pickup list + health/armor awards (the replay model from §3).
}

void WDLAggPlayer::RecordHitscanAccuracy(unsigned /*hitsOnTarget*/, unsigned /*maxShots*/,
                                         int /*targetId*/, team_t /*enemyTeam*/, int /*angleBits*/,
                                         int /*ax*/, int /*ay*/, int /*az*/, int /*tx*/, int /*ty*/,
                                         int /*tz*/, int /*mod*/, int /*ticsElapsed*/,
                                         WDLGameTypeV6 /*gameType*/)
{
	// TODO(S4c): hitscan accuracy cone trig.
}

void WDLAggPlayer::RecordProjectileAccuracy(unsigned /*hitsOnTarget*/, unsigned /*maxShots*/,
                                            int /*targetId*/, team_t /*enemyTeam*/,
                                            int /*angleBits*/, int /*ax*/, int /*ay*/, int /*az*/,
                                            int /*tx*/, int /*ty*/, int /*tz*/, int /*mod*/,
                                            int /*ticsElapsed*/, WDLGameTypeV6 /*gameType*/)
{
	// TODO(S4c): projectile accuracy.
}

void WDLAggPlayer::RecordTracerAccuracy(unsigned /*hitsOnTarget*/, unsigned /*maxShots*/,
                                        int /*targetId*/, team_t /*enemyTeam*/, int /*angleBits*/,
                                        int /*ax*/, int /*ay*/, int /*az*/, int /*tx*/, int /*ty*/,
                                        int /*tz*/, int /*mod*/, int /*ticsElapsed*/,
                                        WDLGameTypeV6 /*gameType*/)
{
	// TODO(S4c): tracer accuracy.
}

void WDLAggPlayer::RecordPlayerSpawn(int /*spawnId*/, int /*ticsElapsed*/)
{
	// TODO(S4b): player-spawn list.
}

void WDLAggPlayer::RecordPlayerBeacon(int /*angleBits*/, int /*ax*/, int /*ay*/, int /*az*/,
                                      int /*ticsElapsed*/)
{
	// TODO(S4e): beacon list (replay/realtime aux data).
}

void WDLAggPlayer::RecordProjectileFire(int /*angleBits*/, int /*ticsElapsed*/, int /*mod*/,
                                        int /*ax*/, int /*ay*/, int /*az*/)
{
	// TODO(S4c): projectile-fire list (pairs with projectile accuracy).
}

void WDLAggPlayer::FinalizeGame()
{
	// TODO(S4b+): close out in-flight state (final spree, etc.).
}

// ---------------------------------------------------------------------------
// WDLAggGame
// ---------------------------------------------------------------------------

WDLAggGame::WDLAggGame(WDLGameTypeV6 gameType, int beginTic, int endGameTic)
    : m_gameType(gameType), m_beginTic(beginTic), m_endGameTic(endGameTic)
{
}

void WDLAggGame::AddPlayers(const WDLPlayers& players)
{
	m_players.clear();
	m_players.reserve(players.size());
	for (const auto& p : players)
		m_players.emplace_back(p);
}

WDLAggPlayer* WDLAggGame::FindByNetId(int netid)
{
	for (auto& p : m_players)
	{
		if (p.netid == netid)
			return &p;
	}
	return nullptr;
}

void WDLAggGame::Aggregate(const WDLEventLog& events)
{
	// Online: consume events in recorded (gametic) order, advancing the state
	// machine on each one. (The recorder appends events in order.)
	for (const auto& ev : events)
		Dispatch(ev);

	// Finalize anything still in flight.
	for (auto& p : m_players)
		p.FinalizeGame();
}

// Faithful port of WdlLogFile.ParseV6's per-event switch. Player handles are
// resolved by netid (FirstOrDefault — see FindByNetId). ticsElapsed is the
// match-relative tic: duration - (endGameTic - ev.gametic) == ev.gametic -
// beginTic. Coordinates are passed straight from apos/tpos so the aggregator
// receives exactly the units the legacy parser received from the text log.
void WDLAggGame::Dispatch(const WDLEvent& ev)
{
	WDLAggPlayer* activator = FindByNetId(ev.activator);
	WDLAggPlayer* target = FindByNetId(ev.target);

	const int targetId = target ? target->id : 0;
	const int ticsElapsed = ev.gametic - m_beginTic;
	const team_t enemyTeam = target ? target->team : TEAM_NONE;

	const bool teamGame =
	    m_gameType == WDLGameTypeV6::TeamDeathmatch || m_gameType == WDLGameTypeV6::CaptureTheFlag;

	switch (ev.ev)
	{
	case WDL_EVENT_DAMAGE:
	case WDL_EVENT_CARRIERDAMAGE:
	case WDL_EVENT_ENVIRODAMAGE:
	case WDL_EVENT_ENVIROCARRIERDAMAGE:
	{
		// (Legacy parser dereferences the target unconditionally; we guard so a
		// malformed stream can't crash the server. Valid logs always carry one.)
		if (target == nullptr)
			break;

		target->TakeDamageFromPlayer(ev.arg0, ev.arg1, target->GetArmorType());

		if (activator == nullptr)
		{
			// No activator: only environmental damage is awarded (self damage
			// without an activator is dropped, matching the parser).
			if (ev.ev == WDL_EVENT_ENVIRODAMAGE || ev.ev == WDL_EVENT_ENVIROCARRIERDAMAGE)
			{
				target->AwardDamageToPlayer(-1, "World", ev.arg0, ev.arg1, target->GetArmorType(),
				                            WDLDamageTypeV6::EnvironmentalDamage, ev.arg2,
				                            target->PlayerHasFlag(), false, ticsElapsed, ev.apos[0],
				                            ev.apos[1], ev.apos[2], ev.tpos[0], ev.tpos[1],
				                            ev.tpos[2]);
			}
		}
		else if (activator->id == target->id)
		{
			target->AwardDamageToPlayer(target->id, target->name, ev.arg0, ev.arg1,
			                            target->GetArmorType(), WDLDamageTypeV6::SelfDamage, ev.arg2,
			                            target->PlayerHasFlag(), false, ticsElapsed, ev.apos[0],
			                            ev.apos[1], ev.apos[2], ev.tpos[0], ev.tpos[1], ev.tpos[2]);
		}
		else
		{
			WDLDamageTypeV6 damageType =
			    teamGame && activator->team == target->team
			        ? WDLDamageTypeV6::DamageByTeammate
			        : WDLDamageTypeV6::DamageByEnemyPlayer;

			activator->AwardDamageToPlayer(target->id, target->name, ev.arg0, ev.arg1,
			                               target->GetArmorType(), damageType, ev.arg2,
			                               activator->PlayerHasFlag(), target->PlayerHasFlag(),
			                               ticsElapsed, ev.apos[0], ev.apos[1], ev.apos[2],
			                               ev.tpos[0], ev.tpos[1], ev.tpos[2]);
		}
		break;
	}

	case WDL_EVENT_KILL:
	case WDL_EVENT_CARRIERKILL:
	case WDL_EVENT_ENVIROKILL:
	case WDL_EVENT_ENVIROCARRIERKILL:
	{
		if (target == nullptr)
			break;

		WDLDeathTypeV6 deathType;
		const int killedId = target->id;

		if (activator == nullptr)
		{
			deathType = (ev.ev == WDL_EVENT_ENVIROKILL || ev.ev == WDL_EVENT_ENVIROCARRIERKILL)
			                ? WDLDeathTypeV6::Environmental
			                : WDLDeathTypeV6::Suicide;
		}
		else if (activator->name == target->name) // suicide-with-flag edge case
		{
			deathType = WDLDeathTypeV6::Suicide;
		}
		else
		{
			const bool isTeamKill = teamGame && activator->team == target->team;
			deathType = WDLDeathTypeV6::KilledByPlayer;
			activator->AwardKill(target->PlayerHasFlag(), activator->PlayerHasFlag(), isTeamKill,
			                     ev.arg2, ticsElapsed, killedId, target->name, ev.tpos[0],
			                     ev.tpos[1], ev.tpos[2], ev.apos[0], ev.apos[1], ev.apos[2]);
		}

		target->PlayerKilled(ticsElapsed, deathType, ev.arg2, ev.tpos[0], ev.tpos[1], ev.tpos[2]);
		break;
	}

	case WDL_EVENT_TOUCH:       // FlagTouch
	case WDL_EVENT_PICKUPTOUCH: // FlagPickupTouch
	case WDL_EVENT_CARRYRETURNFLAG:
	{
		if (activator == nullptr)
			break;

		WDLFlagTouchTypeV6 touchType;
		if (ev.ev == WDL_EVENT_TOUCH)
			touchType = WDLFlagTouchTypeV6::FlagTouch;
		else if (ev.ev == WDL_EVENT_PICKUPTOUCH)
			touchType = WDLFlagTouchTypeV6::PickupFlagTouch;
		else
			touchType = WDLFlagTouchTypeV6::CarryReturnFlagTouch;

		// TODO(S4d): per-team assist stacks — a FlagTouch resets the activator's
		// team stack and pushes the toucher; a PickupFlagTouch pushes onto it.

		activator->PlayerTouchedFlag(touchType, ticsElapsed, ev.apos[0], ev.apos[1], ev.apos[2]);
		break;
	}

	case WDL_EVENT_CAPTURE:       // FlagCapture
	case WDL_EVENT_PICKUPCAPTURE: // FlagPickupCapture
	{
		if (activator == nullptr)
			break;

		const bool isPickupCapture = (ev.ev == WDL_EVENT_PICKUPCAPTURE);
		activator->AwardFlagCapture(ticsElapsed, isPickupCapture, ev.apos[0], ev.apos[1],
		                            ev.apos[2]);

		// TODO(S4d): record the FlagAssistTable entry and, for pickup captures,
		// pop the capturer off the team stack and award assists to the rest.
		break;
	}

	case WDL_EVENT_PICKUPITEM:
		if (activator != nullptr)
			activator->AwardPickup(ev.arg0, ev.arg1, ev.arg2 > 0, ticsElapsed);
		break;

	case WDL_EVENT_RETURNFLAG:
		if (activator != nullptr)
			activator->AwardFlagReturn(ev.apos[0], ev.apos[1], ev.apos[2], ticsElapsed);
		break;

	case WDL_EVENT_ASSIST:
		// Not implemented in v6 (assists are derived from the touch stacks).
		break;

	case WDL_EVENT_SPREADACCURACY:
	case WDL_EVENT_SSACCURACY:
		if (activator != nullptr)
			activator->RecordHitscanAccuracy(static_cast<unsigned>(ev.arg2),
			                                 static_cast<unsigned>(ev.arg3), targetId, enemyTeam,
			                                 ev.arg0, ev.apos[0], ev.apos[1], ev.apos[2], ev.tpos[0],
			                                 ev.tpos[1], ev.tpos[2], ev.arg1, ticsElapsed,
			                                 m_gameType);
		break;

	case WDL_EVENT_TRACERACCURACY:
		if (activator != nullptr)
			activator->RecordTracerAccuracy(static_cast<unsigned>(ev.arg2),
			                                static_cast<unsigned>(ev.arg3), targetId, enemyTeam,
			                                ev.arg0, ev.apos[0], ev.apos[1], ev.apos[2], ev.tpos[0],
			                                ev.tpos[1], ev.tpos[2], ev.arg1, ticsElapsed,
			                                m_gameType);
		break;

	case WDL_EVENT_PROJACCURACY:
		if (activator != nullptr)
			activator->RecordProjectileAccuracy(static_cast<unsigned>(ev.arg2),
			                                    static_cast<unsigned>(ev.arg3), targetId, enemyTeam,
			                                    ev.arg0, ev.apos[0], ev.apos[1], ev.apos[2],
			                                    ev.tpos[0], ev.tpos[1], ev.tpos[2], ev.arg1,
			                                    ticsElapsed, m_gameType);
		break;

	case WDL_EVENT_SPAWNPLAYER:
		if (activator != nullptr)
			activator->RecordPlayerSpawn(ev.arg2, ticsElapsed);
		break;

	case WDL_EVENT_SPAWNITEM:
		// TODO(S4e): item-spawn event list (arg1 = spawn id, arg0 = pickup type).
		// Note: the legacy parser builds this list but GameV6 does not emit it.
		break;

	case WDL_EVENT_JOINGAME:
	case WDL_EVENT_DISCONNECT:
		// TODO(S4): ingress/egress list (arg1 = netid, arg0 = team). The legacy
		// parser tracks these but GameV6 does not currently emit them.
		break;

	case WDL_EVENT_PLAYERBEACON:
		if (activator != nullptr)
			activator->RecordPlayerBeacon(ev.arg0, ev.apos[0], ev.apos[1], ev.apos[2], ticsElapsed);
		break;

	case WDL_EVENT_PROJFIRE:
		if (activator != nullptr)
			activator->RecordProjectileFire(ev.arg0, ticsElapsed, ev.arg1, ev.apos[0], ev.apos[1],
			                                ev.apos[2]);
		break;

	default:
		break;
	}
}

// ---------------------------------------------------------------------------

WDLAggGame M_AggregateWDLStatsV6(WDLGameTypeV6 gameType, int beginTic, int endGameTic,
                                 const WDLPlayers& players, const WDLEventLog& events)
{
	WDLAggGame game(gameType, beginTic, endGameTic);
	game.AddPlayers(players);
	game.Aggregate(events);
	return game;
}
