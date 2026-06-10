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
//   In-engine aggregation of recorded WDL events into the WDLStats v6
//   (GameV6) shape — the C++ port of WDLStatLogParserService's WdlLogFile +
//   PlayerV6. Reproduces the legacy parser's output from the in-memory event
//   stream, computed incrementally as the match runs (no text-file round-trip).
//
//   S4a (this chunk): the dispatch loop + event ingestion. The per-event
//   routing, player-handle resolution, event-relative tic, and faithful
//   coordinate/arg pass-through are implemented here. The per-player stat
//   simulation and GameV6 assembly are filled in by later chunks:
//     S4b — non-CTF player stats (health/armor sim, kills, damage, sprees)
//     S4c — accuracy / cone trig
//     S4d — CTF flag lifecycle + assists
//     S4e — WDLAggGame assembly + GameEvents[]
//
//-----------------------------------------------------------------------------

#pragma once

#include <string>
#include <vector>

#include "m_wdlstats.h" // shared recording tables (WDLEvent, WDLPlayer, …)

// v6 game type — mirrors LogFileEnumsV6.GameType (the wire contract). The caller
// translates the engine's game mode into this when invoking the aggregator.
enum class WDLGameTypeV6
{
	Coop = 0,
	DeathMatch = 1,
	TeamDeathmatch = 2,
	CaptureTheFlag = 3,
	Horde = 4,
};

// Classifications derived during dispatch — mirror the v6 wire enums. (Mod and
// pickup ids stay as engine-native ints in the aggregator and are cast to the
// v6 Mods/Pickups enums at serialization time, exactly as the legacy parser
// does; that is also where the known Pickups enum drift is reconciled.)
enum class WDLDamageTypeV6
{
	DamageByEnemyPlayer = 0,
	DamageByTeammate = 1,
	SelfDamage = 2,
	EnvironmentalDamage = 3,
};

enum class WDLDeathTypeV6
{
	KilledByPlayer = 0,
	Environmental = 1,
	Suicide = 2,
};

enum class WDLFlagTouchTypeV6
{
	FlagTouch = 0,
	PickupFlagTouch = 1,
	CarryReturnFlagTouch = 2,
};

enum class WDLArmorV6
{
	None = 0,
	GreenArmor = 1,
	BlueArmor = 2,
};

// ---------------------------------------------------------------------------
// WDLAggPlayer — per-player aggregation state (port of PlayerV6).
//
// S4a establishes identity + the handler entry points the dispatch calls, with
// signatures mirroring PlayerV6 exactly. Bodies (the health/armor simulation and
// the derived stat lists) are implemented in S4b–S4d.
// ---------------------------------------------------------------------------
class WDLAggPlayer
{
  public:
	explicit WDLAggPlayer(const WDLPlayer& src);

	int id;           // 1-based table id
	int netid;        // engine netid (pid); events reference players by this
	std::string name;
	team_t team;
	std::string sub;  // Keycloak subject (empty for anon/bots)

	// Query helpers used by the dispatch.
	bool PlayerHasFlag() const;       // S4d
	WDLArmorV6 GetArmorType() const;  // S4b

	// Event handlers — called by WDLAggGame::Dispatch with faithfully mapped
	// fields/units. Implementations land in S4b–S4d.
	void TakeDamageFromPlayer(int health, int armor, WDLArmorV6 armorType);
	void AwardDamageToPlayer(int damagedId, const std::string& damagedName, int hp, int armor,
	                         WDLArmorV6 armorType, WDLDamageTypeV6 damageType, int mod,
	                         bool selfHasFlag, bool targetHasFlag, int ticsElapsed, int ax, int ay,
	                         int az, int tx, int ty, int tz);
	void AwardKill(bool playerKilledHadFlag, bool playerHadFlag, bool isTeamKill, int mod,
	               int ticsElapsed, int killedId, const std::string& killedName, int tx, int ty,
	               int tz, int ax, int ay, int az);
	void PlayerKilled(int ticsElapsed, WDLDeathTypeV6 deathType, int weaponMod, int x, int y, int z);
	void PlayerTouchedFlag(WDLFlagTouchTypeV6 touchType, int ticsElapsed, int ax, int ay, int az);
	void AwardFlagCapture(int ticsElapsed, bool isPickupCapture, int fx, int fy, int fz);
	void AwardFlagReturn(int ax, int ay, int az, int ticsElapsed);
	void AwardPickup(int pickupType, int itemId, bool dropped, int ticsElapsed);
	void RecordHitscanAccuracy(unsigned hitsOnTarget, unsigned maxShots, int targetId,
	                           team_t enemyTeam, int angleBits, int ax, int ay, int az, int tx,
	                           int ty, int tz, int mod, int ticsElapsed, WDLGameTypeV6 gameType);
	void RecordProjectileAccuracy(unsigned hitsOnTarget, unsigned maxShots, int targetId,
	                              team_t enemyTeam, int angleBits, int ax, int ay, int az, int tx,
	                              int ty, int tz, int mod, int ticsElapsed, WDLGameTypeV6 gameType);
	void RecordTracerAccuracy(unsigned hitsOnTarget, unsigned maxShots, int targetId,
	                          team_t enemyTeam, int angleBits, int ax, int ay, int az, int tx,
	                          int ty, int tz, int mod, int ticsElapsed, WDLGameTypeV6 gameType);
	void RecordPlayerSpawn(int spawnId, int ticsElapsed);
	void RecordPlayerBeacon(int angleBits, int ax, int ay, int az, int ticsElapsed);
	void RecordProjectileFire(int angleBits, int ticsElapsed, int mod, int ax, int ay, int az);

	// Close out anything still in flight at match end (sprees, etc.). S4b+.
	void FinalizeGame();
};

// ---------------------------------------------------------------------------
// WDLAggGame — whole-match aggregation (port of WdlLogFile.ParseV6 dispatch +
// GameV6 assembly). S4a implements the incremental dispatch; assembly is S4e.
// ---------------------------------------------------------------------------
class WDLAggGame
{
  public:
	WDLAggGame(WDLGameTypeV6 gameType, int beginTic, int endGameTic);

	// Build the per-player aggregation state from the recorder's player table.
	void AddPlayers(const WDLPlayers& players);

	// Drive the recorded event stream through the dispatch in gametic order
	// (events are appended in order during the match), then finalize. This is
	// the online path: each event advances the state machine as it is consumed.
	void Aggregate(const WDLEventLog& events);

  private:
	void Dispatch(const WDLEvent& ev);

	// Resolve a player by engine netid. Mirrors the legacy parser's
	// FirstOrDefault: the FIRST table entry with this netid (so reconnect/
	// team-switch duplicates resolve the legacy v6 way), or nullptr.
	WDLAggPlayer* FindByNetId(int netid);

	WDLGameTypeV6 m_gameType;
	int m_beginTic;
	int m_endGameTic;
	std::vector<WDLAggPlayer> m_players;
};

// Build and run the v6 aggregator over a recorded match. Returns the populated
// game ready for serialization (S5). Wiring this into match end / upload is
// S5/S6; it is defined now so the dispatch compiles and can be unit-tested.
WDLAggGame M_AggregateWDLStatsV6(WDLGameTypeV6 gameType, int beginTic, int endGameTic,
                                 const WDLPlayers& players, const WDLEventLog& events);
