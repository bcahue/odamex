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
//   In-engine accumulation of WDLStats v6 (GameV6) stats. WDLAggGame /
//   WDLAggPlayer are a passive data structure that the recorder (m_wdlstats.cpp)
//   updates as the match is played: each event awards directly to the player(s)
//   involved (no event-dispatch / re-parse, no end-of-match replay). Only the
//   GameV6 *schema* is shared with WDLStatLogParserService — the C# parser is
//   kept solely as a parity reference run over the same match's text log.
//
//   Output shapes (the JSON contract) live at the bottom of this header; the
//   per-player award handlers and the GameV6 assembly are in the .cpp.
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

// Engine armortype (0=none, 1=green, 2=blue) -> v6 Armor enum. Used by the
// recorder (m_wdlstats.cpp) when awarding stats from live engine state.
WDLArmorV6 WDLMapArmorType(int armortype);

// Multi-kill window: max tics between frags to still count toward a multi-kill
// (ParserConstants.MaxMultiKillGameticDuration).
static const int WDL_MAX_MULTIKILL_TICS = 35 * 3;

// Intermediate per-event records the aggregator accumulates (ports of the
// parser's Domain models). Coordinates are in the units the recorder stored
// (passed through from apos/tpos). Mod/pickup ids stay engine-native here and
// are cast to the v6 wire enums at serialization (S5).
struct WDLAggDamage
{
	int damagedId;
	std::string damagedName;
	int mod;
	WDLArmorV6 armorType;
	int hp;
	int armor;
	int ax, ay, az; // activator position
	int tx, ty, tz; // target position
	int gameTic;
	// Classification carried on the record so the canonical damage stream can be
	// merged live (same-tic scan-back) and routed into the output lists at Build.
	WDLDamageTypeV6 damageType = WDLDamageTypeV6::DamageByEnemyPlayer;
	bool playerHadFlag = false; // activator carried a flag when the hit landed
	bool enemyHadFlag = false;  // DAMAGE vs CARRIERDAMAGE (merge key)
};

struct WDLAggKill
{
	int mod;
	int killedId;
	std::string killedName;
	int tx, ty, tz; // killed (target) position
	int ax, ay, az; // fragger (activator) position
	int gameTic;
};

struct WDLAggPickupRec
{
	int pickupType;
	int itemSpawnId;
	bool dropped;
	int gameTic;
};

struct WDLAggSpawnRec
{
	int spawnId;
	int gameTic;
};

struct WDLAggAccuracy
{
	int weapon; // engine mod; cast to v6 Mods at serialize
	int targetId;
	unsigned hitsOnTarget;
	unsigned maxShots;
	double spritePercent;
	double pinpointPercent;
	int gameTic;
	int ax, ay, az; // attacker position
	int tx, ty, tz; // target position
	int angleBits;
	// Routing context carried on the canonical record so accuracy can be bucketed
	// at Build (mirrors the args RouteAccuracy needs). enemyTeam is the target's
	// team; hasFlag is the shooter's flag state captured when the shot was logged.
	team_t enemyTeam = TEAM_NONE;
	bool hasFlag = false;
};

struct WDLAggProjFire
{
	int angleBits;
	int weapon;
	int ax, ay, az;
	int gameTic;
};

// Accuracy cone-trig constants (ParserConstants). AngleFraction divides the
// engine's integer angle into the 0..2π byte angle; the SSG/non-SSG factors set
// the spread half-cone used to score a hit.
static const double WDL_ANGLE_FRACTION = 1073741824.0;
static const double WDL_ANGLE_SSG_FACTOR = 0.195476876 / 2;
static const double WDL_ANGLE_NONSSG_FACTOR = 0.097738438 / 2;

// CTF flag-lifecycle records (S4d).
struct WDLAggFlagTouch
{
	int touchHp;
	WDLArmorV6 touchArmorType;
	WDLFlagTouchTypeV6 touchType;
	int touchArmor;
	int gameTic;
	int ax, ay, az; // flag position
};

struct WDLAggCapture
{
	int gameTic;
	int captureTicDuration;
	int captureHp;
	WDLArmorV6 captureArmorType;
	int captureArmor;
	bool hasFlagTouch; // false only on malformed input (parser would NRE)
	WDLAggFlagTouch flagTouchResultingInCapture;
	int ax, ay, az; // flag position
};

struct WDLAggFlagReturnRec
{
	int gameTic;
	int ax, ay, az;
};

struct WDLAggAssistTouch
{
	int gameTic;
	int playerId; // table id of the toucher
	std::string playerName;
};

// FlagCaptureTableV6: a capture plus its assist chain (top-of-stack first, as
// the legacy parser's Stack.ToList() produced it).
struct WDLAggFlagCaptureEntry
{
	int captureTic;
	team_t team;
	std::vector<WDLAggAssistTouch> assists;
};

// ---------------------------------------------------------------------------
// WDLAggPlayer — per-player aggregation state (port of PlayerV6).
//
// S4a established identity + the handler entry points the dispatch calls. S4b
// (this chunk) fills in the non-CTF simulation: the health/armor replay model,
// damage/kill/death lists, pickups, spawns, and spree/multi-kill tracking. The
// flag-possession state exists here but is only driven once S4d wires the CTF
// touch/capture handlers; accuracy lists are S4c.
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

	// --- accumulated stats (S4b non-CTF; flag-tagged lists fill in via S4d) ---
	// Canonical streams, populated live as events arrive (same-tic merge applied
	// in place). The categorized output lists below are derived from these once at
	// FinalizeGame — that is pure bucketing for output, not a re-simulation.
	std::vector<WDLAggDamage> damageAll;
	std::vector<WDLAggAccuracy> accuracyAll;
	// Damage lists
	std::vector<WDLAggDamage> damageList;
	std::vector<WDLAggDamage> damageWhileHoldingFlagList;
	std::vector<WDLAggDamage> damageDealtToFlagCarriersList;
	std::vector<WDLAggDamage> damageDealtToFlagCarriersWhileHoldingFlagList;
	std::vector<WDLAggDamage> damageOutputBetweenTouchAndCaptureList;
	std::vector<int> damageOutputBetweenTouchAndCaptureNumberList;
	std::vector<WDLAggDamage> teammateDamageList;
	std::vector<WDLAggDamage> teammateDamageWithFlagList;
	std::vector<WDLAggDamage> selfDamageList;
	std::vector<WDLAggDamage> selfDamageWithFlagList;
	std::vector<WDLAggDamage> environmentalDamageList;
	std::vector<WDLAggDamage> environmentalDamageWithFlagList;
	// Kill / death lists
	std::vector<WDLAggKill> killList;
	std::vector<WDLAggKill> carrierKillList;
	std::vector<WDLAggKill> carrierKillListWithFlagInHand;
	std::vector<WDLAggKill> killListWithFlagInHand;
	std::vector<WDLAggKill> teamKillList;
	std::vector<WDLAggKill> selfKillList;
	std::vector<WDLAggKill> selfKillListWithFlag;
	std::vector<WDLAggKill> environmentalDeath;
	std::vector<WDLAggKill> environmentalDeathWithFlag;
	// Pickups / spawns
	std::vector<WDLAggPickupRec> pickupsList;
	std::vector<WDLAggSpawnRec> playerSpawns;
	// Accuracy lists (S4c). Only populated for team games (TDM/CTF), matching the
	// parser. Teammate-accuracy lists are tracked for fidelity but unused by GameV6.
	std::vector<WDLAggAccuracy> accuracyList;
	std::vector<WDLAggAccuracy> accuracyWithFlagList;
	std::vector<WDLAggAccuracy> accuracyWithoutFlagList;
	std::vector<WDLAggAccuracy> teammateAccuracyList;
	std::vector<WDLAggAccuracy> teammateAccuracyWithFlagList;
	std::vector<WDLAggProjFire> projectileFires;
	// CTF flag lifecycle (S4d)
	std::vector<WDLAggCapture> captureList;
	std::vector<WDLAggCapture> pickupCaptureList;
	std::vector<WDLAggFlagTouch> totalFlagTouches;
	std::vector<WDLAggFlagTouch> totalReturnFlagTouches;
	std::vector<WDLAggFlagTouch> flagTouchesThatResultedInCapture;
	std::vector<WDLAggFlagReturnRec> flagReturnEvents;
	// Scalar counters
	int assists = 0;
	int flagReturns = 0;
	int capturesWithSuperPickups = 0;
	int healthGainedFromPowerPickups = 0;
	int healthGainedFromNonPowerPickups = 0;
	int highestAmountOfKillsBeforeCapturing = 0;
	int deaths = 0;
	int totalDeaths = 0;
	int flagCarrierDeaths = 0;
	int suicides = 0;
	int suicidesWithFlag = 0;
	int environmentalDeaths = 0;
	int environmentalFlagCarrierDeaths = 0;
	int longestSpree = 0;
	int highestMultiKill = 0;
	int totalPowerPickups = 0;

	// Award handlers — called by the recorder (m_wdlstats.cpp) as the match is
	// played. Ground-truth values (armor type/amount, health, flag possession) are
	// read off the live player_t at the event's source (§3 (B)); nothing here
	// simulates engine state.
	void AwardDamageToPlayer(int damagedId, const std::string& damagedName, int hp, int armor,
	                         WDLArmorV6 armorType, WDLDamageTypeV6 damageType, int mod,
	                         bool selfHasFlag, bool targetHasFlag, int ticsElapsed, int ax, int ay,
	                         int az, int tx, int ty, int tz);
	void AwardKill(bool playerKilledHadFlag, bool playerHadFlag, bool isTeamKill, int mod,
	               int ticsElapsed, int killedId, const std::string& killedName, int tx, int ty,
	               int tz, int ax, int ay, int az);
	void PlayerKilled(int ticsElapsed, WDLDeathTypeV6 deathType, int weaponMod, bool hadFlag, int x,
	                  int y, int z);
	void PlayerTouchedFlag(WDLFlagTouchTypeV6 touchType, int ticsElapsed, int touchHp, int touchArmor,
	                       WDLArmorV6 touchArmorType, int ax, int ay, int az);
	void AwardFlagCapture(int ticsElapsed, bool isPickupCapture, int captureHp, int captureArmor,
	                      WDLArmorV6 captureArmorType, int fx, int fy, int fz);
	void AwardFlagReturn(int ax, int ay, int az, int ticsElapsed);
	void AwardPickup(int pickupType, int itemId, bool dropped, int ticsElapsed, int healthGained);
	void RecordHitscanAccuracy(unsigned hitsOnTarget, unsigned maxShots, int targetId,
	                           team_t enemyTeam, int angleBits, int ax, int ay, int az, int tx,
	                           int ty, int tz, int mod, int ticsElapsed, bool hasFlag);
	void RecordProjectileAccuracy(unsigned hitsOnTarget, unsigned maxShots, int targetId,
	                              team_t enemyTeam, int angleBits, int ax, int ay, int az, int tx,
	                              int ty, int tz, int mod, int ticsElapsed, bool hasFlag);
	void RecordTracerAccuracy(unsigned hitsOnTarget, unsigned maxShots, int targetId,
	                          team_t enemyTeam, int angleBits, int ax, int ay, int az, int tx,
	                          int ty, int tz, int mod, int ticsElapsed, bool hasFlag);
	void RecordPlayerSpawn(int spawnId, int ticsElapsed);
	void RecordPlayerBeacon(int angleBits, int ax, int ay, int az, int ticsElapsed);
	void RecordProjectileFire(int angleBits, int ticsElapsed, int mod, int ax, int ay, int az);

	// Close out anything still in flight at match end (sprees) and bucket the
	// canonical damage/accuracy streams into the categorized output lists. Routing
	// needs the game type (accuracy is only bucketed for team games). Idempotent.
	void FinalizeGame(WDLGameTypeV6 gameType);

  private:
	void HandleMultiKillAndSpree(int gameTic);
	// Bucket damageAll into the categorized output lists (called once at finalize).
	void RouteDamageOutput();
	bool m_finalized = false;
	// Shared accuracy routing (team/flag/hit buckets); used by all three
	// accuracy recorders. Only records for team games, mirroring the parser.
	void RouteAccuracy(const WDLAggAccuracy& acc, team_t enemyTeam, unsigned hits,
	                   WDLGameTypeV6 gameType, bool hasFlag);

	// --- derived state only (NO simulated engine state, §3 (B)) ---
	// Spree / multi-kill tracking, derived from the kill/death sequence.
	int m_consecutiveKills = 0;
	int m_multiKillCounter = 0;
	int m_lastKillGameTic = 0;
	// Touch→capture correlation, derived from the flag-touch sequence.
	bool m_isPickupTouch = false;
	int m_currentKillsWhileHoldingFlag = 0;
	int m_lastTouchGameTic = 0;
	int m_lastPickupTouchGameTic = 0;
	// Damage dealt since the current flag touch; committed on capture.
	std::vector<WDLAggDamage> m_tempDamageOutputBetweenTouchAndCapture;
	// The in-flight flag touch that will become FlagTouchResultingInCapture.
	bool m_hasCurrentFlagTouch = false;
	WDLAggFlagTouch m_currentFlagTouch{};
};

// ===========================================================================
// Output views — the assembled GameV6 shape (S4e). Field names mirror the v6
// JSON contract (camelCase) so the S5 serializer maps 1:1. TimeSpan-typed
// contract fields are stored as double total-seconds (computed exactly as the
// parser does) and formatted by S5; Date is carried as the metadata's string.
// ===========================================================================

struct WDLWadV6
{
	std::string filename;
	std::string hash;
};

struct WDLItemSpawnsV6
{
	int id = 0;
	int x = 0, y = 0, z = 0;
	int pickup = 0;
};

struct WDLPlayerSpawnsV6
{
	int id = 0;
	int x = 0, y = 0, z = 0;
	int team = 0;
};

struct WDLFlagLocationsV6
{
	int team = 0;
	int x = 0, y = 0, z = 0;
};

struct WDLGameMetaDataV6
{
	int parserVersion = 0;
	std::string date;
	std::string mapNumber; // "MAP01"
	std::string mapName;
	int lives = 0;
	int gameType = 0;
	int attackDefend = 0;
	std::string originalLogFileName;
	int durationTics = 0;
	int durationSeconds = 0; // TimeSpan = FromSeconds(durationTics/35)
	int round = 0;
	int winResult = 0;
	int winId = 0;
	std::string hostName;
};

struct WDLDamageAggregateV6
{
	int totalDamage = 0;
	int totalDamageGreenArmor = 0;
	int totalDamageBlueArmor = 0;
	std::string targetName;
	int targetId = 0;
	int weapon = 0;
};

struct WDLKillAggregateV6
{
	int totalKills = 0;
	std::string targetName;
	int targetId = 0;
	int weapon = 0;
};

struct WDLAccuracyAggregateV6
{
	double pinpointPercentage = 0;
	double spritePercentage = 0;
	unsigned totalShotsHit = 0;
	unsigned totalShotsMissed = 0;
	unsigned totalShotsAttempted = 0;
	unsigned totalPelletsHit = 0;
	unsigned totalPelletsMissed = 0;
	unsigned totalPelletsAttempted = 0;
	double hitMissRatio = 0;
	int weapon = 0;
};

struct WDLPickupAggregateV6
{
	int pickupType = 0;
	int totalPickups = 0;
};

struct WDLPlayerStatsV6
{
	int id = 0;
	int netId = 0;
	std::string name;
	std::string sub;
	int team = 0;
	int assists = 0;
	int captures = 0;
	int pickupCaptures = 0;
	int capturesWithSuperPickup = 0;
	int flagTouches = 0;
	int pickupFlagTouches = 0;
	int damageOutputBetweenTouchAndCaptureMin = 0;
	int damageOutputBetweenTouchAndCaptureMax = 0;
	double damageOutputBetweenTouchAndCaptureAvg = 0;
	double captureTimeMin = 0; // seconds
	double captureTimeMax = 0;
	double captureTimeAvg = 0;
	int captureTimeMinTics = 0;
	int captureTimeMaxTics = 0;
	double captureTimeAvgTics = 0;
	int captureHealthMin = 0;
	int captureHealthMax = 0;
	double captureHealthAvg = 0;
	int captureGreenArmorMin = 0;
	int captureGreenArmorMax = 0;
	double captureGreenArmorAvg = 0;
	int captureBlueArmorMin = 0;
	int captureBlueArmorMax = 0;
	double captureBlueArmorAvg = 0;
	int flagCarriersKilledWhileHoldingFlag = 0;
	int highestKillsBeforeCapturing = 0;
	double pickupCaptureTimeMin = 0;
	double pickupCaptureTimeMax = 0;
	double pickupCaptureTimeAvg = 0;
	int pickupCaptureTimeMinTics = 0;
	int pickupCaptureTimeMaxTics = 0;
	double pickupCaptureTimeAvgTics = 0;
	int totalDamage = 0;
	int totalTeamDamage = 0;
	int selfDamage = 0;
	int selfDamageWithFlag = 0;
	int totalGreenArmorDamage = 0;
	int totalBlueArmorDamage = 0;
	int totalDamageToFlagCarriers = 0;
	int totalDamageAsFlagCarrier = 0;
	int totalDamageToFlagCarriersWhileHoldingFlag = 0;
	int totalDamageTakenFromEnvironment = 0;
	int totalDamageTakenFromEnvironmentAsFlagCarrier = 0;
	int totalFlagReturns = 0;
	int totalKills = 0;
	int killsWithFlag = 0;
	double killDeathRatio = 0;
	double capturePercentage = 0;
	double pickupCapturePercentage = 0;
	double overallCapturePercentage = 0;
	int flagDefenses = 0;
	int totalDeaths = 0;
	int deaths = 0;
	int flagCarrierDeaths = 0;
	int suicides = 0;
	int suicidesWithFlag = 0;
	int environmentalDeaths = 0;
	int environmentalDeathsAsFlagCarrier = 0;
	int teamKills = 0;
	int longestSpree = 0;
	int highestMultiKill = 0;
	int totalPowerPickups = 0;
	int totalHealthFromPickups = 0;
	int healthFromNonPowerPickups = 0;
	int healthFromPowerPickups = 0;
	int touchHealthMin = 0;
	int touchHealthMax = 0;
	double touchHealthAvg = 0;
	int touchGreenArmorMin = 0;
	int touchGreenArmorMax = 0;
	double touchGreenArmorAvg = 0;
	int touchBlueArmorMin = 0;
	int touchBlueArmorMax = 0;
	double touchBlueArmorAvg = 0;
	int touchHealthResultCaptureMin = 0;
	int touchHealthResultCaptureMax = 0;
	double touchHealthResultCaptureAvg = 0;
	int touchesOverOneHundredHealth = 0;
	std::vector<WDLDamageAggregateV6> damageList;
	std::vector<WDLDamageAggregateV6> damageWithFlagList;
	std::vector<WDLDamageAggregateV6> damageToFlagCarriersList;
	std::vector<WDLDamageAggregateV6> damageToFlagCarriersWithFlagList;
	std::vector<WDLKillAggregateV6> killsList;
	std::vector<WDLKillAggregateV6> killsWithFlagList;
	std::vector<WDLKillAggregateV6> carrierKillList;
	std::vector<WDLKillAggregateV6> carrierKillsWhileHoldingFlagList;
	std::vector<WDLAccuracyAggregateV6> accuracyList;
	std::vector<WDLAccuracyAggregateV6> accuracyWithFlagList;
	std::vector<WDLAccuracyAggregateV6> accuracyWithoutFlagList;
	std::vector<WDLPickupAggregateV6> pickupList;
};

struct WDLTeamStatsV6
{
	int points = 0;
	int captures = 0;
	int pickupCaptures = 0;
	int assists = 0;
	int flagTouches = 0;
	int pickupFlagTouches = 0;
	double totalCapturePercentage = 0;
	double pickupCapturePercentage = 0;
	double capturePercentage = 0;
	int frags = 0;
	int deaths = 0;
	double killDeathRatio = 0;
	int damage = 0;
	int flagDefenses = 0;
	int powerPickups = 0;
	std::vector<std::string> teamPlayers;
};

struct WDLKillDeathEventV6
{
	std::string killerName;
	int killerId = 0;
	int killerX = 0, killerY = 0, killerZ = 0;
	std::string targetName;
	int targetId = 0;
	int targetX = 0, targetY = 0, targetZ = 0;
	int weapon = 0;
};

struct WDLFlagAssistDataV6
{
	int flagTouchTimeTics = 0;
	double flagTouchTime = 0; // seconds
	int playerId = 0;
	std::string playerName;
};

struct WDLFlagTouchCapturesV6
{
	int timeCapturedTics = 0;
	double timeCaptured = 0; // seconds
	int team = 0;
	std::vector<WDLFlagAssistDataV6> flagAssists;
};

struct WDLFlagAssistTableV6
{
	std::vector<WDLFlagTouchCapturesV6> flagTouchCaptures;
};

struct WDLAccuracyEventV6
{
	std::string shooterName;
	std::string targetName;
	int weapon = 0;
	unsigned shotsHit = 0;
	unsigned maxShots = 0;
	double spritePercent = 0;
	double pinpointPercent = 0;
	int angleBits = 0;
	int activatorX = 0, activatorY = 0, activatorZ = 0;
	int targetX = 0, targetY = 0, targetZ = 0;
};

struct WDLDamageEventV6
{
	std::string shooterName;
	std::string targetName;
	int damageType = 0;
	int activatorX = 0, activatorY = 0, activatorZ = 0;
	int targetX = 0, targetY = 0, targetZ = 0;
	int hp = 0;
	int blueArmor = 0;
	int greenArmor = 0;
};

struct WDLKillEventV6
{
	std::string shooterName;
	std::string targetName;
	int weapon = 0;
	int activatorX = 0, activatorY = 0, activatorZ = 0;
	int targetX = 0, targetY = 0, targetZ = 0;
};

struct WDLPickupEventV6
{
	std::string playerName;
	int type = 0;
	int activatorX = 0, activatorY = 0, activatorZ = 0;
};

struct WDLFlagEventV6 // shared shape for touch/capture/return events
{
	std::string playerName;
	int activatorX = 0, activatorY = 0, activatorZ = 0;
};

struct WDLGameEventsV6
{
	int gameTic = 0;
	std::vector<WDLAccuracyEventV6> accuracyEvents;
	std::vector<WDLKillEventV6> kills;
	std::vector<WDLKillEventV6> carrierKills;
	std::vector<WDLKillEventV6> suicides;
	std::vector<WDLKillEventV6> suicidesWithFlag;
	std::vector<WDLKillEventV6> environmentalDeaths;
	std::vector<WDLKillEventV6> environmentalDeathsWithFlag;
	std::vector<WDLDamageEventV6> damage;
	std::vector<WDLDamageEventV6> selfDamage;
	std::vector<WDLDamageEventV6> selfDamageWithFlag;
	std::vector<WDLDamageEventV6> environmentalDamage;
	std::vector<WDLDamageEventV6> environmentalDamageWithFlag;
	std::vector<WDLPickupEventV6> pickups;
	std::vector<WDLFlagEventV6> pickupFlagTouches;
	std::vector<WDLFlagEventV6> flagTouches;
	std::vector<WDLFlagEventV6> flagCaptures;
	std::vector<WDLFlagEventV6> flagReturns;
};

struct WDLGameV6
{
	WDLGameMetaDataV6 metaData;
	std::vector<WDLWadV6> wads;
	std::vector<WDLItemSpawnsV6> itemSpawns;
	std::vector<WDLPlayerSpawnsV6> playerSpawns;
	WDLTeamStatsV6 redTeamStats;
	WDLTeamStatsV6 blueTeamStats;
	WDLTeamStatsV6 greenTeamStats;
	WDLFlagAssistTableV6 flagAssistTable;
	std::vector<WDLPlayerStatsV6> playerStats;
	std::vector<WDLKillDeathEventV6> playerKillDeath;
	std::vector<WDLKillDeathEventV6> playerFlagCarrierKillDeath;
	std::vector<WDLFlagLocationsV6> flagLocations;
	std::vector<WDLGameEventsV6> gameEvents;
};

// Match metadata supplied by the recorder at assembly time (the header values
// the legacy log carried). Pass-through into GameMetaDataV6.
struct WDLAggMeta
{
	int version = 6;
	std::string date; // ISO-8601 from the recorder
	int levelNum = 0;
	std::string levelName;
	int lives = 0;
	int gameType = 0;
	int attackDefend = 0;
	int durationTics = 0;
	int round = 0;
	int winResult = 0;
	int winId = 0;
	std::string hostName;
	std::string originalLogFileName;
};

// ---------------------------------------------------------------------------
// WDLAggGame — whole-match stat accumulator. It is a passive data structure the
// recorder (m_wdlstats.cpp) updates *as the match is played*: each M_Log* event
// resolves the player(s) involved (PlayerByNetId) and calls the relevant award
// handler directly. There is no event-dispatch / re-parse layer — the engine is
// the state machine; this just records what it reports, then assembles GameV6.
// ---------------------------------------------------------------------------
class WDLAggGame
{
  public:
	// Default-constructed games are empty/inert; the recorder assigns a fresh one
	// at match start (it is held as a plain static instance, gated by recording).
	WDLAggGame() = default;
	WDLAggGame(WDLGameTypeV6 gameType, int beginTic, int endGameTic);

	// Append any newly-joined players from the recorder's table (append-only,
	// id == index + 1) so events can resolve them as they arrive.
	void SyncPlayers(const WDLPlayers& players);

	// Resolve a player by engine netid. Mirrors the legacy parser's FirstOrDefault:
	// the FIRST table entry with this netid (so reconnect/team-switch duplicates
	// resolve the legacy v6 way), or nullptr. Called by the recorder per event.
	WDLAggPlayer* PlayerByNetId(int netid);

	bool IsTeamGame() const
	{
		return m_gameType == WDLGameTypeV6::TeamDeathmatch ||
		       m_gameType == WDLGameTypeV6::CaptureTheFlag;
	}

	// Cross-player CTF handlers (the assist stacks and capture table live here, so
	// these can't be plain per-player awards). Called from the recorder's flag
	// touch/capture sites with ground truth read off the live player_t.
	void OnFlagTouch(WDLAggPlayer* activator, WDLFlagTouchTypeV6 touchType, int ticsElapsed, int hp,
	                 int armor, WDLArmorV6 armorType, int ax, int ay, int az);
	void OnFlagCapture(WDLAggPlayer* activator, bool isPickupCapture, int ticsElapsed, int hp,
	                   int armor, WDLArmorV6 armorType, int fx, int fy, int fz);

	// Close out per-player in-flight state and bucket the canonical streams into
	// the output lists. Must run before Build. Idempotent.
	void Finalize();

	// Assemble the finished GameV6 from the accumulated per-player state plus the
	// match metadata and map tables supplied by the recorder (S4e).
	WDLGameV6 Build(const WDLAggMeta& meta, const WDLItemSpawns& itemSpawns,
	                const WDLPlayerSpawns& playerSpawns, const WDLFlagLocations& flagLocations,
	                const std::vector<WDLWadV6>& wads) const;

  private:
	// --- assembly (S4e) ---
	void GenerateTeamStats(WDLGameV6& out) const;
	void GeneratePlayerStats(WDLGameV6& out) const;
	void GenerateKillDeaths(WDLGameV6& out) const;
	void GenerateGameEvents(WDLGameV6& out) const;
	void GenerateFlagAssistTable(WDLGameV6& out) const;
	const WDLAggPlayer* FindByTableId(int tableId) const;
	std::string NameForTableId(int tableId) const;

	// The per-team assist stack for the given team (S4d).
	std::vector<WDLAggAssistTouch>& AssistStackFor(team_t team);

	WDLGameTypeV6 m_gameType = WDLGameTypeV6::Coop;
	int m_beginTic = 0;
	int m_endGameTic = 0;
	std::vector<WDLAggPlayer> m_players;

	// CTF assist tracking (S4d): per-team stacks of flag touches, popped/awarded
	// on capture, plus the assembled capture table.
	std::vector<WDLAggAssistTouch> m_redAssists;
	std::vector<WDLAggAssistTouch> m_blueAssists;
	std::vector<WDLAggAssistTouch> m_greenAssists;
	std::vector<WDLAggFlagCaptureEntry> m_flagCaptureTable;
};

// Bridge: assemble a finished GameV6 from the recorder's current in-memory
// tables + match metadata (level/levelstate/cvars + wad hashes). Implemented in
// m_wdlstats.cpp, which owns those tables. Consumed by the server serializer.
WDLGameV6 M_BuildWDLGameV6();
