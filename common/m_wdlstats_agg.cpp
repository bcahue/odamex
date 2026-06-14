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
//   WDLStats v6 stat accumulation — per-player award handlers + GameV6 assembly.
//   The recorder (m_wdlstats.cpp) drives these directly as the match is played;
//   see m_wdlstats_agg.h for the overall model.
//
//-----------------------------------------------------------------------------

#include "odamex.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <utility>

#include "g_gametype.h"
#include "p_local.h"
#include "m_wdlstats_agg.h"

namespace
{
// Pi constant, in protobreak (C++20) use
// std::numbers:pi instead
const double kPi = 3.14159265358979323846;
} // namespace

// Engine armortype (0=none, 1=green, 2=blue) -> v6 Armor enum.
WDLArmorV6 WDLMapArmorType(int armortype)
{
	if (armortype == 2)
		return WDLArmorV6::BlueArmor;
	if (armortype == 1)
		return WDLArmorV6::GreenArmor;
	return WDLArmorV6::None;
}

// ---------------------------------------------------------------------------
// WDLAggPlayer
// ---------------------------------------------------------------------------

WDLAggPlayer::WDLAggPlayer(const WDLPlayer& src)
    : id(src.id), netid(src.pid), name(src.netname), team(src.team), sub(src.sub)
{
}

// Each piece of damage is folded into the
// canonical stream the instant it is recorded. This allows
// multiple damage events that fire on the same tic (like damage calculated per pellet)
// to be aggregrated as part of the same record.

// The merge key is (damagedId, damageType, enemyHadFlag) — that is, the same target.
void WDLAggPlayer::AwardDamageToPlayer(int damagedId, const std::string& damagedName, int hp,
                                       int armor, WDLArmorV6 armorType, WDLDamageTypeV6 damageType,
                                       int mod, bool playerHasFlag, bool enemyHasFlag,
                                       int matchTic, int ax, int ay, int az, int tx, int ty,
                                       int tz)
{
	const bool isEnviro = (damageType == WDLDamageTypeV6::EnvironmentalDamage);

	// Canonical stream: merge this piece into the matching same-tic record, or
	// start a new one. (Walk back only while still in the same tic.)
	bool merged = false;
	if (!isEnviro)
	{
		for (auto it = damageAll.rbegin(); it != damageAll.rend(); ++it)
		{
			if (it->gameTic != matchTic)
				break;
			if (it->damagedId == damagedId && it->damageType == damageType &&
			    it->enemyHadFlag == enemyHasFlag)
			{
				it->hp += hp;
				it->armor += armor;
				merged = true;
				break;
			}
		}
	}
	if (!merged)
	{
		WDLAggDamage dmg{damagedId, damagedName, mod, armorType, hp, armor,
		                 ax,        ay,          az,  tx,        ty, tz,
		                 matchTic};
		dmg.damageType = damageType;
		dmg.playerHadFlag = playerHasFlag;
		dmg.enemyHadFlag = enemyHasFlag;
		damageAll.push_back(dmg);
	}

	// Touch -> capture damage window is consumed live in AwardFlagCapture, so it has
	// to be maintained as damage happens — only damage dealt while carrying a flag
	// to an enemy (the two "holding flag" output branches) counts toward it.
	const bool holdingFlagOutput = playerHasFlag && !isEnviro &&
	                               damageType != WDLDamageTypeV6::SelfDamage &&
	                               damageType != WDLDamageTypeV6::DamageByTeammate;
	if (holdingFlagOutput)
	{
		bool tmerged = false;
		for (auto it = m_tempDamageOutputBetweenTouchAndCapture.rbegin();
		     it != m_tempDamageOutputBetweenTouchAndCapture.rend(); ++it)
		{
			if (it->gameTic != matchTic)
				break;
			if (it->damagedId == damagedId && it->enemyHadFlag == enemyHasFlag)
			{
				it->hp += hp;
				it->armor += armor;
				tmerged = true;
				break;
			}
		}
		if (!tmerged)
		{
			WDLAggDamage dmg{damagedId, damagedName, mod, armorType, hp, armor,
			                 ax,        ay,          az,  tx,        ty, tz,
			                 matchTic};
			dmg.damageType = damageType;
			dmg.playerHadFlag = playerHasFlag;
			dmg.enemyHadFlag = enemyHasFlag;
			m_tempDamageOutputBetweenTouchAndCapture.push_back(dmg);
		}
	}
}

// Bucket the canonical damage stream into the categorized output lists. Mirrors
// the original AwardDamageToPlayer routing exactly (minus the temp window, which
// is maintained live).
// 
// Run once, at FinalizeGame.
void WDLAggPlayer::RouteDamageOutput()
{
	for (const auto& dmg : damageAll)
	{
		const bool playerHasFlag = dmg.playerHadFlag;
		const bool enemyHasFlag = dmg.enemyHadFlag;

		if (playerHasFlag && dmg.damageType == WDLDamageTypeV6::SelfDamage)
		{
			selfDamageWithFlagList.push_back(dmg);
		}
		else if (dmg.damageType == WDLDamageTypeV6::SelfDamage && !playerHasFlag)
		{
			selfDamageList.push_back(dmg);
		}
		else if (playerHasFlag && dmg.damageType == WDLDamageTypeV6::DamageByTeammate)
		{
			teammateDamageWithFlagList.push_back(dmg);
		}
		else if (!playerHasFlag && dmg.damageType == WDLDamageTypeV6::DamageByTeammate)
		{
			teammateDamageList.push_back(dmg);
		}
		else if (playerHasFlag && dmg.damageType == WDLDamageTypeV6::EnvironmentalDamage)
		{
			environmentalDamageWithFlagList.push_back(dmg);
		}
		else if (!playerHasFlag && dmg.damageType == WDLDamageTypeV6::EnvironmentalDamage)
		{
			environmentalDamageList.push_back(dmg);
		}
		else if (playerHasFlag && !enemyHasFlag)
		{
			damageList.push_back(dmg);
			damageWhileHoldingFlagList.push_back(dmg);
		}
		else if (playerHasFlag && enemyHasFlag)
		{
			damageList.push_back(dmg);
			damageDealtToFlagCarriersWhileHoldingFlagList.push_back(dmg);
			damageWhileHoldingFlagList.push_back(dmg);
		}
		else if (!playerHasFlag && enemyHasFlag)
		{
			damageList.push_back(dmg);
			damageDealtToFlagCarriersList.push_back(dmg);
		}
		else
		{
			damageList.push_back(dmg);
		}
	}
}

void WDLAggPlayer::AwardKill(bool playerKilledHadFlag, bool fraggerHasFlag, bool isTeamKill, int mod,
                            int matchTic, int killedId, const std::string& killedName, int tx,
                            int ty, int tz, int ax, int ay, int az)
{
	WDLAggKill kill{mod, killedId, killedName, tx, ty, tz, ax, ay, az, matchTic};

	// Team kills count toward neither sprees nor multi-kills.
	if (isTeamKill)
	{
		teamKillList.push_back(kill);
	}
	else if (playerKilledHadFlag && !fraggerHasFlag)
	{
		carrierKillList.push_back(kill);
		killList.push_back(kill);
		HandleMultiKillAndSpree(matchTic);
	}
	else if (playerKilledHadFlag && fraggerHasFlag)
	{
		carrierKillListWithFlagInHand.push_back(kill);
		carrierKillList.push_back(kill);
		killList.push_back(kill);
		HandleMultiKillAndSpree(matchTic);
		m_currentKillsWhileHoldingFlag++;
	}
	else if (!playerKilledHadFlag && !fraggerHasFlag)
	{
		killList.push_back(kill);
		HandleMultiKillAndSpree(matchTic);
	}
	else if (!playerKilledHadFlag && fraggerHasFlag)
	{
		killList.push_back(kill);
		killListWithFlagInHand.push_back(kill);
		HandleMultiKillAndSpree(matchTic);
		m_currentKillsWhileHoldingFlag++;
	}
}

void WDLAggPlayer::PlayerKilled(int matchTic, WDLDeathTypeV6 deathType, int weaponMod,
                                bool hadFlag, int x, int y, int z)
{
	switch (deathType)
	{
	case WDLDeathTypeV6::Environmental:
		if (hadFlag)
		{
			totalDeaths++;
			environmentalFlagCarrierDeaths++;
			environmentalDeathWithFlag.push_back(
			    WDLAggKill{MOD_SLIME, id, name, x, y, z, 0, 0, 0, matchTic});
		}
		else
		{
			totalDeaths++;
			environmentalDeaths++;
			environmentalDeath.push_back(
			    WDLAggKill{MOD_SLIME, id, name, x, y, z, 0, 0, 0, matchTic});
		}
		break;

	case WDLDeathTypeV6::KilledByPlayer:
		if (hadFlag)
		{
			totalDeaths++;
			flagCarrierDeaths++;
		}
		else
		{
			totalDeaths++;
			deaths++;
		}
		break;

	case WDLDeathTypeV6::Suicide:
		if (hadFlag)
		{
			totalDeaths++;
			suicidesWithFlag++;
			selfKillListWithFlag.push_back(
			    WDLAggKill{weaponMod, id, name, x, y, z, 0, 0, 0, matchTic});
		}
		else
		{
			totalDeaths++;
			suicides++;
			selfKillList.push_back(WDLAggKill{weaponMod, id, name, x, y, z, 0, 0, 0, matchTic});
		}
		break;
	}

	if (m_consecutiveKills > longestSpree)
		longestSpree = m_consecutiveKills;

	// Reset per-life derived state (no engine state to reset under native capture).
	m_consecutiveKills = 0;
	m_multiKillCounter = 0;
	m_currentKillsWhileHoldingFlag = 0;
	m_tempDamageOutputBetweenTouchAndCapture.clear();
}

void WDLAggPlayer::PlayerTouchedFlag(WDLFlagTouchTypeV6 touchType, int matchTic, int touchHp,
                                     int touchArmor, WDLArmorV6 touchArmorType, int ax, int ay,
                                     int az)
{
	WDLAggFlagTouch touch{touchHp, touchArmorType, touchType, touchArmor, matchTic, ax, ay, az};

	if (touchType != WDLFlagTouchTypeV6::CarryReturnFlagTouch)
	{
		m_currentFlagTouch = touch;
		totalFlagTouches.push_back(touch);
	}
	else
	{
		totalReturnFlagTouches.push_back(touch);
	}
}

void WDLAggPlayer::AwardFlagCapture(int matchTic, bool isPickupCapture, int captureHp,
                                    int captureArmor, WDLArmorV6 captureArmorType, int fx, int fy,
                                    int fz)
{
	// m_currentFlagTouch could be null in some bizarro edge case.
	// I'd rather crash us here to find out why that'd be the case.
	const int duration = matchTic - m_currentFlagTouch.gameTic;

	WDLAggCapture cap{matchTic,
	                  duration,
	                  captureHp,
	                  captureArmorType,
	                  captureArmor,
	                  m_currentFlagTouch,
	                  fx,
	                  fy,
	                  fz};

	if (isPickupCapture)
		pickupCaptureList.push_back(cap);
	else
		captureList.push_back(cap);

	// This could technically enable a player with a single health bonus over
	// 100hp to count as a "super pickup" capture, but that's faithful to the original
	if (captureHp > 100 || captureArmorType == WDLArmorV6::BlueArmor)
		capturesWithSuperPickups++;

	if (m_currentKillsWhileHoldingFlag > highestAmountOfKillsBeforeCapturing)
		highestAmountOfKillsBeforeCapturing = m_currentKillsWhileHoldingFlag;

	int tempSum = 0;
	for (const auto& d : m_tempDamageOutputBetweenTouchAndCapture)
	{
		damageOutputBetweenTouchAndCaptureList.push_back(d);
		tempSum += d.hp;
	}
	damageOutputBetweenTouchAndCaptureNumberList.push_back(tempSum);

	flagTouchesThatResultedInCapture.push_back(m_currentFlagTouch);

	// reset flag-touch correlation state
	m_tempDamageOutputBetweenTouchAndCapture.clear();
	m_currentKillsWhileHoldingFlag = 0;
}

void WDLAggPlayer::AwardFlagReturn(int ax, int ay, int az, int matchTic)
{
	flagReturns++;
	flagReturnEvents.push_back(WDLAggFlagReturnRec{matchTic, ax, ay, az});
}

void WDLAggPlayer::AwardPickup(int pickupType, int itemId, bool dropped, int matchTic,
                              int healthGained)
{
	pickupsList.push_back(WDLAggPickupRec{pickupType, itemId, dropped, matchTic});

	if (pickupType == WDL_PICKUP_SOULSPHERE || pickupType == WDL_PICKUP_MEGASPHERE ||
	    pickupType == WDL_PICKUP_BLUEARMOR || pickupType == WDL_PICKUP_BERSERK)
	{
		totalPowerPickups++;
	}

	if (pickupType == WDL_PICKUP_SOULSPHERE || pickupType == WDL_PICKUP_MEGASPHERE ||
	    pickupType == WDL_PICKUP_BERSERK)
	{
		healthGainedFromPowerPickups += healthGained;
	}
	else if (pickupType == WDL_PICKUP_STIMPACK || pickupType == WDL_PICKUP_MEDKIT ||
	         pickupType == WDL_PICKUP_HEALTHBONUS)
	{
		healthGainedFromNonPowerPickups += healthGained;
	}
}

namespace
{
// Cone-trig for one hitscan accuracy record (extracted so a live hit can re-run
// it on the accumulated shot count).

// In v7, cast standardized_x/y to double and use atan2 to simplify the trig and eliminate
// NaN edge cases.
WDLAggAccuracy ComputeHitscanAcc(int mod, unsigned hitsOnTarget, unsigned maxShots, int targetId,
                                 int angleBits, int ax, int ay, int az, int tx, int ty, int tz,
                                 int tic)
{
	const double aim_theta = angleBits / WDL_ANGLE_FRACTION * (2.0 * kPi);

	const int standardized_x = tx - ax;
	const int standardized_y = ty - ay;

	double target_theta;
	if (standardized_x == 0)
	{
		target_theta = standardized_y < 0 ? kPi * 3 / 2 : kPi / 2;
	}
	else if (standardized_y == 0)
	{
		target_theta = standardized_x < 0 ? kPi : 0.0;
	}
	else
	{
		const double tan_value = std::atan(static_cast<double>(standardized_y / standardized_x));
		if (standardized_x > 0 && standardized_y > 0)
			target_theta = tan_value; // Q1
		else if (standardized_x < 0 && standardized_y > 0)
			target_theta = kPi + tan_value; // Q2
		else if (standardized_x < 0 && standardized_y < 0)
			target_theta = kPi + tan_value; // Q3
		else
			target_theta = (kPi * 2) + tan_value; // Q4
	}

	const double max_angle = std::max(target_theta, aim_theta);
	const double min_angle = std::min(target_theta, aim_theta);
	const double angle_diff = (max_angle - min_angle > kPi) ? min_angle + (2.0 * kPi) - max_angle
	                                                        : max_angle - min_angle;

	double d = std::sqrt(
	    static_cast<double>(standardized_x * standardized_x + standardized_y * standardized_y));
	if (std::isnan(d))
		d = 0.0;

	const double factor =
	    (mod == MOD_SSHOTGUN) ? WDL_ANGLE_SSG_FACTOR : WDL_ANGLE_NONSSG_FACTOR;
	const double top_spread_y = (factor * d) + 16.0;
	const double top_aim_y = std::tan(angle_diff) * d;

	WDLAggAccuracy acc{};
	if (top_aim_y > top_spread_y)
	{
		// Complete miss.
		acc = WDLAggAccuracy{mod, 0,  0,  maxShots, 0.0, 0.0, tic,
		                     ax,  ay, az, tx,       ty,  tz,  angleBits};
	}
	else if (top_aim_y <= 16.0)
	{
		// Crosshair was on the target.
		double pinpoint = std::fabs(100.0 - (top_aim_y / top_spread_y * 100.0));
		if (pinpoint > 100.0)
			pinpoint = 100.0;
		acc = WDLAggAccuracy{mod, targetId, hitsOnTarget, maxShots, 100.0, pinpoint, tic,
		                     ax,  ay,       az,           tx,       ty,    tz,       angleBits};
	}
	else
	{
		// In the spread near the target.
		double pinpoint = std::fabs(100.0 - (top_aim_y / top_spread_y * 100.0));
		if (pinpoint > 100.0)
			pinpoint = 100.0;
		const double sprite =
		    std::fabs(100.0 - ((top_aim_y - 16.0) / (top_spread_y - 16.0) * 100.0));
		acc = WDLAggAccuracy{mod, targetId, hitsOnTarget, maxShots, sprite, pinpoint, tic,
		                     ax,  ay,       az,           tx,       ty,     tz,       angleBits};
	}

	// Registered a hit without the angle math placing us on the target.
	if (acc.targetId == 0 && hitsOnTarget > 0)
	{
		acc = WDLAggAccuracy{mod, targetId, hitsOnTarget, maxShots, 0.0, 0.0, tic,
		                     ax,  ay,       az,           tx,       ty,  tz,  angleBits};
	}

	return acc;
}

// Fold one accuracy piece into a player's canonical accuracy stream, applying
// the same same-tic shot/hit merge the recorder's LogAccuracyShot/LogAccuracyHit
// apply: the first shot for a (tic, weapon) creates the record, and subsequent
// hits accumulate into it (re-running the trig on the running hit total).
void MergeAccuracy(std::vector<WDLAggAccuracy>& list, bool hitscan, unsigned hits,
                   unsigned maxShots, int targetId, team_t enemyTeam, int angleBits, int ax, int ay,
                   int az, int tx, int ty, int tz, int mod, int tic, bool hasFlag)
{
	auto build = [&](unsigned h, int tgt, int btx, int bty, int btz, int bangle, int bax, int bay,
	                 int baz) -> WDLAggAccuracy {
		WDLAggAccuracy a;
		if (hitscan)
			a = ComputeHitscanAcc(mod, h, maxShots, tgt, bangle, bax, bay, baz, btx, bty, btz, tic);
		else
			// Projectiles / tracers carry no cone trig — percentages stay 0.
			a = WDLAggAccuracy{mod, tgt, h, maxShots, 0.0, 0.0, tic,
			                   bax, bay, baz, btx, bty, btz, bangle};
		a.enemyTeam = enemyTeam;
		a.hasFlag = hasFlag;
		return a;
	};

	if (hits == 0)
	{
		// Shot attempt: dedupe per (tic, weapon).
		for (auto it = list.rbegin(); it != list.rend(); ++it)
		{
			if (it->gameTic != tic)
				break;
			if (it->weapon == mod)
				return;
		}
		list.push_back(build(0, 0, tx, ty, tz, angleBits, ax, ay, az));
		return;
	}

	// Hit: accumulate into this tic's shot for the weapon/target (LogAccuracyHit).
	for (auto it = list.rbegin(); it != list.rend(); ++it)
	{
		if (it->gameTic != tic)
			break;
		if (it->weapon != mod)
			continue;
		if (it->targetId != targetId && it->targetId != 0)
			continue;
		const unsigned total = it->hitsOnTarget + hits;
		const bool shotHadFlag = it->hasFlag; // flag state is the shot's, kept on merge
		// Recompute from the shot's angle/position (the recorder keeps those) plus
		// this hit's target position.
		WDLAggAccuracy upd = build(total, targetId, tx, ty, tz, it->angleBits, it->ax, it->ay,
		                           it->az);
		upd.hasFlag = shotHadFlag;
		*it = upd;
		return;
	}

	// Hit with no recorded shot this tic — create the record from it.
	list.push_back(build(hits, targetId, tx, ty, tz, angleBits, ax, ay, az));
}
} // namespace

void WDLAggPlayer::RecordHitscanAccuracy(unsigned hitsOnTarget, unsigned maxShots, int targetId,
                                         team_t enemyTeam, int angleBits, int ax, int ay, int az,
                                         int tx, int ty, int tz, int mod, int matchTic,
                                         bool hasFlag)
{
	MergeAccuracy(accuracyAll, true, hitsOnTarget, maxShots, targetId, enemyTeam, angleBits, ax, ay,
	              az, tx, ty, tz, mod, matchTic, hasFlag);
}

void WDLAggPlayer::RecordProjectileAccuracy(unsigned hitsOnTarget, unsigned maxShots, int targetId,
                                            team_t enemyTeam, int angleBits, int ax, int ay, int az,
                                            int tx, int ty, int tz, int mod, int matchTic,
                                            bool hasFlag)
{
	MergeAccuracy(accuracyAll, false, hitsOnTarget, maxShots, targetId, enemyTeam, angleBits, ax, ay,
	              az, tx, ty, tz, mod, matchTic, hasFlag);
}

void WDLAggPlayer::RecordTracerAccuracy(unsigned hitsOnTarget, unsigned maxShots, int targetId,
                                        team_t enemyTeam, int angleBits, int ax, int ay, int az,
                                        int tx, int ty, int tz, int mod, int matchTic,
                                        bool hasFlag)
{
	MergeAccuracy(accuracyAll, false, hitsOnTarget, maxShots, targetId, enemyTeam, angleBits, ax, ay,
	              az, tx, ty, tz, mod, matchTic, hasFlag);
}

void WDLAggPlayer::RouteAccuracy(const WDLAggAccuracy& acc, team_t enemyTeam, unsigned hits,
                                 WDLGameTypeV6 gameType, bool hasFlag)
{
	// Accuracy is only bucketed for team games in v6.
	if (gameType != WDLGameTypeV6::TeamDeathmatch && gameType != WDLGameTypeV6::CaptureTheFlag)
		return;

	const bool hit = hits > 0;
	if (team == enemyTeam && hasFlag && hit)
	{
		teammateAccuracyWithFlagList.push_back(acc);
	}
	else if (team == enemyTeam && !hasFlag && hit)
	{
		teammateAccuracyList.push_back(acc);
	}
	else if (!hit && hasFlag)
	{
		accuracyList.push_back(acc);
		accuracyWithFlagList.push_back(acc);
	}
	else if (!hit && !hasFlag)
	{
		accuracyList.push_back(acc);
		accuracyWithoutFlagList.push_back(acc);
	}
	else if (hit && !hasFlag)
	{
		accuracyList.push_back(acc);
		accuracyWithoutFlagList.push_back(acc);
	}
	else if (hit && hasFlag)
	{
		accuracyList.push_back(acc);
		accuracyWithFlagList.push_back(acc);
	}
}

void WDLAggPlayer::RecordPlayerSpawn(int spawnId, int matchTic)
{
	playerSpawns.push_back(WDLAggSpawnRec{spawnId, matchTic});
}

void WDLAggPlayer::RecordPlayerBeacon(int /*angleBits*/, int /*ax*/, int /*ay*/, int /*az*/,
                                      int /*matchTic*/)
{
	// TODO: beacon list (replay/realtime aux data).
}

void WDLAggPlayer::RecordProjectileFire(int angleBits, int matchTic, int mod, int ax, int ay,
                                        int az)
{
	projectileFires.push_back(WDLAggProjFire{angleBits, mod, ax, ay, az, matchTic});
}

void WDLAggPlayer::FinalizeGame(WDLGameTypeV6 gameType)
{
	if (m_finalized)
		return;
	m_finalized = true;

	// Record the highest spree if the player never died.
	if (m_consecutiveKills > longestSpree)
		longestSpree = m_consecutiveKills;

	// Bucket the canonical streams into the categorized output lists. This is pure
	// formatting of records already awarded live — no event stream is replayed.
	RouteDamageOutput();
	for (const auto& acc : accuracyAll)
		RouteAccuracy(acc, acc.enemyTeam, acc.hitsOnTarget, gameType, acc.hasFlag);
}

void WDLAggPlayer::HandleMultiKillAndSpree(int gameTic)
{
	m_consecutiveKills++;
	if (gameTic <= m_lastKillGameTic + WDL_MAX_MULTIKILL_TICS || m_lastKillGameTic == 0)
	{
		m_multiKillCounter++;
		if (m_multiKillCounter > highestMultiKill)
			highestMultiKill = m_multiKillCounter;
	}

	m_lastKillGameTic = gameTic;
}

// ---------------------------------------------------------------------------
// WDLAggGame
// ---------------------------------------------------------------------------

WDLAggGame::WDLAggGame(WDLGameTypeV6 gameType, int beginTic, int endGameTic)
    : m_gameType(gameType), m_beginTic(beginTic), m_endGameTic(endGameTic)
{
}

WDLAggPlayer* WDLAggGame::PlayerByNetId(int netid)
{
	for (auto& p : m_players)
	{
		if (p.netid == netid)
			return &p;
	}
	return nullptr;
}

std::vector<WDLAggAssistTouch>& WDLAggGame::AssistStackFor(team_t team)
{
	if (team == TEAM_BLUE)
		return m_blueAssists;
	if (team == TEAM_RED)
		return m_redAssists;
	return m_greenAssists;
}

void WDLAggGame::SyncPlayers(const WDLPlayers& players)
{
	// Live path: the recorder's player table is append-only (id == index + 1), so
	// just add any entries we haven't seen yet, applying the same team
	// normalization AddPlayers does for non-team games.

	for (size_t i = m_players.size(); i < players.size(); i++)
	{
		m_players.emplace_back(players[i]);
		if (!G_IsTeamGame())
			m_players.back().team = TEAM_NONE;
	}
}

void WDLAggGame::Finalize()
{
	for (auto& p : m_players)
		p.FinalizeGame(m_gameType);
}

// Cross-player CTF flag-touch handling. The per-team assist stack lives on the
// game (a touch starts/extends the chain), so this can't be a plain per-player
// award.
void WDLAggGame::OnFlagTouch(WDLAggPlayer* activator, WDLFlagTouchTypeV6 touchType, int matchTic,
                            int hp, int armor, WDLArmorV6 armorType, int ax, int ay, int az)
{
	if (activator == nullptr)
		return;

	if (touchType == WDLFlagTouchTypeV6::FlagTouch)
	{
		// A fresh flag touch resets the team's assist chain.
		std::vector<WDLAggAssistTouch>& stack = AssistStackFor(activator->team);
		stack.clear();
		stack.push_back(WDLAggAssistTouch{matchTic, activator->id, activator->name});
	}
	else if (touchType == WDLFlagTouchTypeV6::PickupFlagTouch)
	{
		// A pickup touch extends the chain (potential assister).
		AssistStackFor(activator->team)
		    .push_back(WDLAggAssistTouch{matchTic, activator->id, activator->name});
	}

	activator->PlayerTouchedFlag(touchType, matchTic, hp, armor, armorType, ax, ay, az);
}

// Cross-player CTF capture handling: awards the capture to the player, snapshots
// the assist chain into the capture table, and (for pickup captures) credits the
// assisters and resets the chain.
void WDLAggGame::OnFlagCapture(WDLAggPlayer* activator, bool isPickupCapture, int matchTic,
                              int hp, int armor, WDLArmorV6 armorType, int fx, int fy, int fz)
{
	if (activator == nullptr)
		return;

	activator->AwardFlagCapture(matchTic, isPickupCapture, hp, armor, armorType, fx, fy,
	                            fz);

	// Snapshot the assist chain (top-of-stack first, as Stack.ToList()) into the
	// capture table before any popping.
	std::vector<WDLAggAssistTouch>& stack = AssistStackFor(activator->team);
	WDLAggFlagCaptureEntry entry;
	entry.captureTic = matchTic;
	entry.team = activator->team;
	for (auto it = stack.rbegin(); it != stack.rend(); ++it)
		entry.assists.push_back(*it);
	m_flagCaptureTable.push_back(std::move(entry));

	if (isPickupCapture)
	{
		// The capturer (top of stack) doesn't assist themselves; everyone else
		// still on the chain earns an assist. Then the chain resets.
		if (!stack.empty())
			stack.pop_back();
		for (const auto& a : stack)
		{
			for (auto& p : m_players)
			{
				if (p.team == activator->team && p.id == a.playerId)
				{
					p.assists++;
					break;
				}
			}
		}
		stack.clear();
	}
}

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Assembly
// ---------------------------------------------------------------------------

namespace
{
// DefaultIfEmpty().Min()/Max()/Average() semantics: 0 over an empty sequence.
int VecMin(const std::vector<int>& v)
{
	if (v.empty())
		return 0;
	int m = v[0];
	for (int x : v)
		m = std::min(m, x);
	return m;
}
int VecMax(const std::vector<int>& v)
{
	if (v.empty())
		return 0;
	int m = v[0];
	for (int x : v)
		m = std::max(m, x);
	return m;
}
double VecAvg(const std::vector<int>& v)
{
	if (v.empty())
		return 0.0;
	long long s = 0;
	for (int x : v)
		s += x;
	return static_cast<double>(s) / static_cast<double>(v.size());
}
double SafeDiv(double n, double d)
{
	return d == 0.0 ? 0.0 : n / d;
}
double TicsToSeconds(double tics)
{
	return tics / 35.0;
}

int SumHp(const std::vector<WDLAggDamage>& v)
{
	int s = 0;
	for (const auto& d : v)
		s += d.hp;
	return s;
}

std::vector<WDLDamageAggregateV6> AggregateDamage(const std::vector<WDLAggDamage>& list)
{
	std::vector<WDLDamageAggregateV6> agg;
	for (const auto& d : list)
	{
		WDLDamageAggregateV6* found = nullptr;
		for (auto& a : agg)
		{
			if (a.targetId == d.damagedId && a.weapon == d.mod)
			{
				found = &a;
				break;
			}
		}
		if (found)
		{
			found->totalDamage += d.hp;
			if (d.armorType == WDLArmorV6::BlueArmor)
				found->totalDamageBlueArmor += d.armor;
			else if (d.armorType == WDLArmorV6::GreenArmor)
				found->totalDamageGreenArmor += d.armor;
		}
		else
		{
			WDLDamageAggregateV6 a;
			a.targetId = d.damagedId;
			a.targetName = d.damagedName;
			a.weapon = d.mod;
			a.totalDamage = d.hp;
			a.totalDamageBlueArmor = d.armorType == WDLArmorV6::BlueArmor ? d.armor : 0;
			a.totalDamageGreenArmor = d.armorType == WDLArmorV6::GreenArmor ? d.armor : 0;
			agg.push_back(std::move(a));
		}
	}
	return agg;
}

std::vector<WDLKillAggregateV6> AggregateKill(const std::vector<WDLAggKill>& list)
{
	std::vector<WDLKillAggregateV6> agg;
	for (const auto& k : list)
	{
		WDLKillAggregateV6* found = nullptr;
		for (auto& a : agg)
		{
			if (a.targetName == k.killedName && a.weapon == k.mod)
			{
				found = &a;
				break;
			}
		}
		if (found)
		{
			found->totalKills += 1;
		}
		else
		{
			WDLKillAggregateV6 a;
			a.totalKills = 1;
			a.targetName = k.killedName;
			a.targetId = k.killedId;
			a.weapon = k.mod;
			agg.push_back(std::move(a));
		}
	}
	return agg;
}

std::vector<WDLAccuracyAggregateV6> AggregateAccuracy(const std::vector<WDLAggAccuracy>& list)
{
	std::vector<WDLAccuracyAggregateV6> agg;
	int oldGameTic = 0;

	for (const auto& acc : list)
	{
		const int gameTic = acc.gameTic;
		WDLAccuracyAggregateV6* found = nullptr;
		for (auto& a : agg)
		{
			if (a.weapon == acc.weapon)
			{
				found = &a;
				break;
			}
		}

		if (found)
		{
			if (gameTic != oldGameTic)
			{
				found->totalPelletsAttempted += acc.maxShots;
				found->totalShotsAttempted += 1;
				if (acc.hitsOnTarget > 0)
					found->totalShotsHit += 1;
			}

			if (acc.hitsOnTarget > 0)
			{
				found->pinpointPercentage += acc.pinpointPercent;
				found->spritePercentage += acc.spritePercent;
				found->totalPelletsHit += acc.hitsOnTarget;
			}
			else if (gameTic != oldGameTic)
			{
				found->totalShotsMissed++;
				oldGameTic = gameTic;
			}
		}
		else
		{
			WDLAccuracyAggregateV6 a;
			a.weapon = acc.weapon;
			a.totalShotsAttempted = 1;
			a.totalPelletsAttempted = acc.maxShots;
			if (acc.hitsOnTarget > 0)
			{
				a.totalShotsHit = 1;
				a.totalShotsMissed = 0;
				a.totalPelletsHit = acc.hitsOnTarget;
				a.pinpointPercentage = acc.pinpointPercent;
				a.spritePercentage = acc.spritePercent;
			}
			else
			{
				a.totalShotsHit = 0;
				a.totalShotsMissed = 1;
				a.totalPelletsHit = 0;
				a.pinpointPercentage = 0.0;
				a.spritePercentage = 0.0;
			}
			agg.push_back(std::move(a));
		}
	}

	for (auto& a : agg)
	{
		a.hitMissRatio = SafeDiv(static_cast<double>(a.totalShotsHit),
		                         static_cast<double>(a.totalShotsAttempted));
		a.spritePercentage = SafeDiv(a.spritePercentage, static_cast<double>(a.totalShotsAttempted));
		a.pinpointPercentage =
		    SafeDiv(a.pinpointPercentage, static_cast<double>(a.totalShotsAttempted));
		if (a.totalPelletsAttempted > a.totalPelletsHit)
			a.totalPelletsMissed += (a.totalPelletsAttempted - a.totalPelletsHit);
	}

	return agg;
}

std::vector<WDLPickupAggregateV6> AggregatePickup(const std::vector<WDLAggPickupRec>& list)
{
	std::vector<WDLPickupAggregateV6> agg;
	for (const auto& p : list)
	{
		WDLPickupAggregateV6* found = nullptr;
		for (auto& a : agg)
		{
			if (a.pickupType == p.pickupType)
			{
				found = &a;
				break;
			}
		}
		if (found)
			found->totalPickups++;
		else
			agg.push_back(WDLPickupAggregateV6{p.pickupType, 1});
	}
	return agg;
}
} // namespace

const WDLAggPlayer* WDLAggGame::FindByTableId(int tableId) const
{
	for (const auto& p : m_players)
	{
		if (p.id == tableId)
			return &p;
	}
	return nullptr;
}

std::string WDLAggGame::NameForTableId(int tableId) const
{
	const WDLAggPlayer* p = FindByTableId(tableId);
	return p ? p->name : std::string();
}

void WDLAggGame::GeneratePlayerStats(WDLGameV6& out) const
{
	for (const auto& p : m_players)
	{
		WDLPlayerStatsV6 s;
		s.id = p.id;
		s.netId = p.netid;
		s.name = p.name;
		s.sub = p.sub;
		s.team = static_cast<int>(p.team);

		int flagTouchCount = 0, pickupFlagTouchCount = 0;
		for (const auto& t : p.totalFlagTouches)
		{
			if (t.touchType == WDLFlagTouchTypeV6::FlagTouch)
				flagTouchCount++;
			else if (t.touchType == WDLFlagTouchTypeV6::PickupFlagTouch)
				pickupFlagTouchCount++;
		}

		s.assists = p.assists;
		s.captures = static_cast<int>(p.captureList.size());
		s.capturesWithSuperPickup = p.capturesWithSuperPickups;
		s.pickupCaptures = static_cast<int>(p.pickupCaptureList.size());
		s.flagTouches = flagTouchCount;
		s.pickupFlagTouches = pickupFlagTouchCount;

		s.damageOutputBetweenTouchAndCaptureMin =
		    VecMin(p.damageOutputBetweenTouchAndCaptureNumberList);
		s.damageOutputBetweenTouchAndCaptureMax =
		    VecMax(p.damageOutputBetweenTouchAndCaptureNumberList);
		s.damageOutputBetweenTouchAndCaptureAvg =
		    VecAvg(p.damageOutputBetweenTouchAndCaptureNumberList);

		// Capture times / health / armor (regular captures).
		std::vector<int> capTimes, capHp, capGreen, capBlue;
		for (const auto& c : p.captureList)
		{
			capTimes.push_back(c.gameTic - c.flagTouchResultingInCapture.gameTic);
			capHp.push_back(c.captureHp);
			if (c.captureArmorType == WDLArmorV6::GreenArmor)
				capGreen.push_back(c.captureArmor);
			else if (c.captureArmorType == WDLArmorV6::BlueArmor)
				capBlue.push_back(c.captureArmor);
		}
		s.captureTimeMinTics = VecMin(capTimes);
		s.captureTimeMaxTics = VecMax(capTimes);
		s.captureTimeAvgTics = VecAvg(capTimes);
		s.captureTimeMin = TicsToSeconds(s.captureTimeMinTics);
		s.captureTimeMax = TicsToSeconds(s.captureTimeMaxTics);
		s.captureTimeAvg = TicsToSeconds(s.captureTimeAvgTics);
		s.captureHealthMin = VecMin(capHp);
		s.captureHealthMax = VecMax(capHp);
		s.captureHealthAvg = VecAvg(capHp);
		s.captureGreenArmorMin = VecMin(capGreen);
		s.captureGreenArmorMax = VecMax(capGreen);
		s.captureGreenArmorAvg = VecAvg(capGreen);
		s.captureBlueArmorMin = VecMin(capBlue);
		s.captureBlueArmorMax = VecMax(capBlue);
		s.captureBlueArmorAvg = VecAvg(capBlue);

		s.flagCarriersKilledWhileHoldingFlag =
		    static_cast<int>(p.carrierKillListWithFlagInHand.size());
		s.highestKillsBeforeCapturing = p.highestAmountOfKillsBeforeCapturing;

		// Pickup-capture times.
		std::vector<int> pcapTimes;
		for (const auto& c : p.pickupCaptureList)
			pcapTimes.push_back(c.gameTic - c.flagTouchResultingInCapture.gameTic);
		s.pickupCaptureTimeMinTics = VecMin(pcapTimes);
		s.pickupCaptureTimeMaxTics = VecMax(pcapTimes);
		s.pickupCaptureTimeAvgTics = VecAvg(pcapTimes);
		s.pickupCaptureTimeMin = TicsToSeconds(s.pickupCaptureTimeMinTics);
		s.pickupCaptureTimeMax = TicsToSeconds(s.pickupCaptureTimeMaxTics);
		s.pickupCaptureTimeAvg = TicsToSeconds(s.pickupCaptureTimeAvgTics);

		s.totalDamage = SumHp(p.damageList);
		s.totalTeamDamage = SumHp(p.teammateDamageList);
		s.selfDamage = SumHp(p.selfDamageList);
		s.selfDamageWithFlag = SumHp(p.selfDamageWithFlagList);
		for (const auto& d : p.damageList)
		{
			if (d.armorType == WDLArmorV6::GreenArmor)
				s.totalGreenArmorDamage += d.armor;
			else if (d.armorType == WDLArmorV6::BlueArmor)
				s.totalBlueArmorDamage += d.armor;
		}
		s.totalDamageToFlagCarriers = SumHp(p.damageDealtToFlagCarriersList);
		s.totalDamageAsFlagCarrier = SumHp(p.damageWhileHoldingFlagList);
		s.totalDamageToFlagCarriersWhileHoldingFlag =
		    SumHp(p.damageDealtToFlagCarriersWhileHoldingFlagList);
		s.totalDamageTakenFromEnvironment = SumHp(p.environmentalDamageList);
		s.totalDamageTakenFromEnvironmentAsFlagCarrier = SumHp(p.environmentalDamageWithFlagList);

		s.totalFlagReturns = p.flagReturns;
		s.totalKills = static_cast<int>(p.killList.size());
		s.killsWithFlag = static_cast<int>(p.killListWithFlagInHand.size());
		s.killDeathRatio = SafeDiv(static_cast<double>(p.killList.size()),
		                           static_cast<double>(p.totalDeaths)) *
		                   100.0;
		s.capturePercentage =
		    SafeDiv(static_cast<double>(p.captureList.size()), static_cast<double>(flagTouchCount)) *
		    100.0;
		s.pickupCapturePercentage = SafeDiv(static_cast<double>(p.pickupCaptureList.size()),
		                                    static_cast<double>(pickupFlagTouchCount)) *
		                            100.0;
		s.overallCapturePercentage =
		    SafeDiv(static_cast<double>(p.captureList.size() + p.pickupCaptureList.size()),
		            static_cast<double>(p.totalFlagTouches.size())) *
		    100.0;
		s.flagDefenses = static_cast<int>(p.carrierKillList.size());
		s.deaths = p.deaths;
		s.totalDeaths = p.totalDeaths;
		s.flagCarrierDeaths = p.flagCarrierDeaths;
		s.suicides = p.suicides;
		s.suicidesWithFlag = p.suicidesWithFlag;
		s.environmentalDeaths = p.environmentalDeaths;
		s.environmentalDeathsAsFlagCarrier = p.environmentalFlagCarrierDeaths;
		s.teamKills = static_cast<int>(p.teamKillList.size());
		s.longestSpree = p.longestSpree;
		s.highestMultiKill = p.highestMultiKill;
		s.totalPowerPickups = p.totalPowerPickups;
		s.totalHealthFromPickups =
		    p.healthGainedFromNonPowerPickups + p.healthGainedFromPowerPickups;
		s.healthFromNonPowerPickups = p.healthGainedFromNonPowerPickups;
		s.healthFromPowerPickups = p.healthGainedFromPowerPickups;

		// Touch health / armor (all touches) and touch→capture health.
		std::vector<int> touchHp, touchGreen, touchBlue, touchCapHp;
		int touchesOver100 = 0;
		for (const auto& t : p.totalFlagTouches)
		{
			touchHp.push_back(t.touchHp);
			if (t.touchArmorType == WDLArmorV6::GreenArmor)
				touchGreen.push_back(t.touchArmor);
			else if (t.touchArmorType == WDLArmorV6::BlueArmor)
				touchBlue.push_back(t.touchArmor);
		}
		for (const auto& t : p.flagTouchesThatResultedInCapture)
		{
			touchCapHp.push_back(t.touchHp);
			if (t.touchHp > 100)
				touchesOver100++;
		}
		s.touchHealthMin = VecMin(touchHp);
		s.touchHealthMax = VecMax(touchHp);
		s.touchHealthAvg = VecAvg(touchHp);
		s.touchGreenArmorMin = VecMin(touchGreen);
		s.touchGreenArmorMax = VecMax(touchGreen);
		s.touchGreenArmorAvg = VecAvg(touchGreen);
		s.touchBlueArmorMin = VecMin(touchBlue);
		s.touchBlueArmorMax = VecMax(touchBlue);
		s.touchBlueArmorAvg = VecAvg(touchBlue);
		s.touchHealthResultCaptureMin = VecMin(touchCapHp);
		s.touchHealthResultCaptureMax = VecMax(touchCapHp);
		s.touchHealthResultCaptureAvg = VecAvg(touchCapHp);
		s.touchesOverOneHundredHealth = touchesOver100;

		// Nested aggregates.
		s.pickupList = AggregatePickup(p.pickupsList);
		s.damageList = AggregateDamage(p.damageList);
		s.damageWithFlagList = AggregateDamage(p.damageWhileHoldingFlagList);
		s.damageToFlagCarriersList = AggregateDamage(p.damageDealtToFlagCarriersList);
		s.damageToFlagCarriersWithFlagList =
		    AggregateDamage(p.damageDealtToFlagCarriersWhileHoldingFlagList);
		s.killsList = AggregateKill(p.killList);
		s.killsWithFlagList = AggregateKill(p.killListWithFlagInHand);
		s.carrierKillList = AggregateKill(p.carrierKillList);
		s.carrierKillsWhileHoldingFlagList = AggregateKill(p.carrierKillListWithFlagInHand);
		s.accuracyList = AggregateAccuracy(p.accuracyList);
		s.accuracyWithFlagList = AggregateAccuracy(p.accuracyWithFlagList);
		s.accuracyWithoutFlagList = AggregateAccuracy(p.accuracyWithoutFlagList);

		out.playerStats.push_back(std::move(s));
	}
}

void WDLAggGame::GenerateTeamStats(WDLGameV6& out) const
{
	auto computeTeam = [this](team_t team) {
		WDLTeamStatsV6 ts;
		for (const auto& p : m_players)
		{
			if (p.team != team)
				continue;
			ts.captures += static_cast<int>(p.captureList.size());
			ts.pickupCaptures += static_cast<int>(p.pickupCaptureList.size());
			ts.assists += p.assists;
			for (const auto& t : p.totalFlagTouches)
			{
				if (t.touchType == WDLFlagTouchTypeV6::FlagTouch)
					ts.flagTouches++;
				else if (t.touchType == WDLFlagTouchTypeV6::PickupFlagTouch)
					ts.pickupFlagTouches++;
			}
			ts.frags += static_cast<int>(p.killList.size());
			ts.deaths += p.totalDeaths;
			ts.damage += SumHp(p.damageList);
			ts.flagDefenses += static_cast<int>(p.carrierKillList.size()) +
			                   static_cast<int>(p.carrierKillListWithFlagInHand.size());
			ts.powerPickups += p.totalPowerPickups;
			ts.teamPlayers.push_back(p.name);
		}
		ts.points = ts.captures + ts.pickupCaptures;
		ts.totalCapturePercentage =
		    SafeDiv(static_cast<double>(ts.points),
		            static_cast<double>(ts.flagTouches) + static_cast<double>(ts.pickupFlagTouches)) *
		    100.0;
		ts.pickupCapturePercentage = SafeDiv(static_cast<double>(ts.pickupCaptures),
		                                     static_cast<double>(ts.pickupFlagTouches)) *
		                             100.0;
		ts.capturePercentage =
		    SafeDiv(static_cast<double>(ts.captures), static_cast<double>(ts.flagTouches)) * 100.0;
		ts.killDeathRatio =
		    SafeDiv(static_cast<double>(ts.frags), static_cast<double>(ts.deaths)) * 100.0;
		return ts;
	};

	out.redTeamStats = computeTeam(TEAM_RED);
	out.blueTeamStats = computeTeam(TEAM_BLUE);
	out.greenTeamStats = computeTeam(TEAM_GREEN);
}

void WDLAggGame::GenerateKillDeaths(WDLGameV6& out) const
{
	for (const auto& p : m_players)
	{
		for (const auto& k : p.killList)
		{
			WDLKillDeathEventV6 e;
			e.killerName = p.name;
			e.killerX = k.ax;
			e.killerY = k.ay;
			e.killerZ = k.az;
			e.targetName = NameForTableId(k.killedId);
			e.targetX = k.tx;
			e.targetY = k.ty;
			e.targetZ = k.tz;
			e.weapon = k.mod;
			out.playerKillDeath.push_back(std::move(e));
		}
		for (const auto& k : p.carrierKillList)
		{
			WDLKillDeathEventV6 e;
			e.killerName = p.name;
			e.killerX = k.ax;
			e.killerY = k.ay;
			e.killerZ = k.az;
			e.targetName = NameForTableId(k.killedId);
			e.targetX = k.tx;
			e.targetY = k.ty;
			e.targetZ = k.tz;
			e.weapon = k.mod;
			out.playerFlagCarrierKillDeath.push_back(std::move(e));
		}
	}
}

void WDLAggGame::GenerateFlagAssistTable(WDLGameV6& out) const
{
	for (const auto& entry : m_flagCaptureTable)
	{
		WDLFlagTouchCapturesV6 cap;
		cap.timeCapturedTics = entry.captureTic;
		cap.timeCaptured = TicsToSeconds(entry.captureTic);
		cap.team = static_cast<int>(entry.team);
		for (const auto& a : entry.assists)
		{
			WDLFlagAssistDataV6 d;
			d.flagTouchTimeTics = a.gameTic;
			d.flagTouchTime = TicsToSeconds(a.gameTic);
			d.playerId = a.playerId;
			d.playerName = a.playerName;
			cap.flagAssists.push_back(std::move(d));
		}
		out.flagAssistTable.flagTouchCaptures.push_back(std::move(cap));
	}
}

void WDLAggGame::GenerateGameEvents(WDLGameV6& out) const
{
	std::unordered_map<int, size_t> ticIndex;
	auto bucket = [&](int gameTic) -> WDLGameEventsV6& {
		auto it = ticIndex.find(gameTic);
		if (it != ticIndex.end())
			return out.gameEvents[it->second];
		WDLGameEventsV6 ev;
		ev.gameTic = gameTic;
		out.gameEvents.push_back(std::move(ev));
		ticIndex[gameTic] = out.gameEvents.size() - 1;
		return out.gameEvents.back();
	};

	auto makeDamage = [](const std::string& shooter, const std::string& target,
	                     const WDLAggDamage& d) {
		WDLDamageEventV6 e;
		e.shooterName = shooter;
		e.targetName = target;
		e.activatorX = d.ax;
		e.activatorY = d.ay;
		e.activatorZ = d.az;
		e.targetX = d.tx;
		e.targetY = d.ty;
		e.targetZ = d.tz;
		e.damageType = d.mod;
		e.hp = d.hp;
		e.blueArmor = d.armorType == WDLArmorV6::BlueArmor ? d.armor : 0;
		e.greenArmor = d.armorType == WDLArmorV6::GreenArmor ? d.armor : 0;
		return e;
	};
	auto makeKill = [](const std::string& shooter, const std::string& target, const WDLAggKill& k) {
		WDLKillEventV6 e;
		e.shooterName = shooter;
		e.targetName = target;
		e.weapon = k.mod;
		e.activatorX = k.ax;
		e.activatorY = k.ay;
		e.activatorZ = k.az;
		e.targetX = k.tx;
		e.targetY = k.ty;
		e.targetZ = k.tz;
		return e;
	};

	for (const auto& p : m_players)
	{
		for (const auto& d : p.damageList)
		{
			const std::string target = d.damagedId == -1 ? "World" : NameForTableId(d.damagedId);
			bucket(d.gameTic).damage.push_back(makeDamage(p.name, target, d));
		}
		for (const auto& d : p.environmentalDamageList)
		{
			const std::string shooter = d.damagedId == -1 ? "World" : NameForTableId(d.damagedId);
			bucket(d.gameTic).environmentalDamage.push_back(makeDamage(shooter, p.name, d));
		}
		for (const auto& d : p.environmentalDamageWithFlagList)
		{
			const std::string shooter = d.damagedId == -1 ? "World" : NameForTableId(d.damagedId);
			bucket(d.gameTic).environmentalDamageWithFlag.push_back(makeDamage(shooter, p.name, d));
		}
		for (const auto& d : p.selfDamageList)
		{
			const std::string shooter = d.damagedId == -1 ? "World" : NameForTableId(d.damagedId);
			bucket(d.gameTic).selfDamage.push_back(makeDamage(shooter, p.name, d));
		}
		for (const auto& d : p.selfDamageWithFlagList)
		{
			const std::string shooter = d.damagedId == -1 ? "World" : NameForTableId(d.damagedId);
			bucket(d.gameTic).selfDamageWithFlag.push_back(makeDamage(shooter, p.name, d));
		}
		for (const auto& a : p.accuracyList)
		{
			WDLAccuracyEventV6 e;
			e.shooterName = p.name;
			e.targetName = a.targetId != 0 ? NameForTableId(a.targetId) : std::string();
			e.angleBits = a.angleBits;
			e.activatorX = a.ax;
			e.activatorY = a.ay;
			e.activatorZ = a.az;
			e.targetX = a.tx;
			e.targetY = a.ty;
			e.targetZ = a.tz;
			e.weapon = a.weapon;
			e.shotsHit = a.hitsOnTarget;
			e.maxShots = a.maxShots;
			e.pinpointPercent = a.pinpointPercent;
			e.spritePercent = a.spritePercent;
			bucket(a.gameTic).accuracyEvents.push_back(std::move(e));
		}
		for (const auto& k : p.killList)
			bucket(k.gameTic).kills.push_back(makeKill(p.name, NameForTableId(k.killedId), k));
		for (const auto& k : p.carrierKillList)
			bucket(k.gameTic).carrierKills.push_back(makeKill(p.name, NameForTableId(k.killedId), k));
		for (const auto& k : p.selfKillList)
			bucket(k.gameTic).suicides.push_back(makeKill(p.name, NameForTableId(k.killedId), k));
		for (const auto& k : p.selfKillListWithFlag)
			bucket(k.gameTic)
			    .suicidesWithFlag.push_back(makeKill(p.name, NameForTableId(k.killedId), k));
		for (const auto& k : p.environmentalDeath)
			bucket(k.gameTic)
			    .environmentalDeaths.push_back(makeKill(p.name, NameForTableId(k.killedId), k));
		for (const auto& k : p.environmentalDeathWithFlag)
			bucket(k.gameTic).environmentalDeathsWithFlag.push_back(
			    makeKill(p.name, NameForTableId(k.killedId), k));
		for (const auto& pk : p.pickupsList)
		{
			WDLPickupEventV6 e;
			e.playerName = p.name;
			e.type = pk.pickupType;
			bucket(pk.gameTic).pickups.push_back(std::move(e));
		}
		for (const auto& t : p.totalFlagTouches)
		{
			WDLFlagEventV6 e;
			e.playerName = p.name;
			e.activatorX = t.ax;
			e.activatorY = t.ay;
			e.activatorZ = t.az;
			if (t.touchType == WDLFlagTouchTypeV6::PickupFlagTouch)
				bucket(t.gameTic).pickupFlagTouches.push_back(std::move(e));
			else if (t.touchType == WDLFlagTouchTypeV6::FlagTouch)
				bucket(t.gameTic).flagTouches.push_back(std::move(e));
		}
		for (const auto& c : p.captureList)
		{
			WDLFlagEventV6 e;
			e.playerName = p.name;
			e.activatorX = c.ax;
			e.activatorY = c.ay;
			e.activatorZ = c.az;
			bucket(c.gameTic).flagCaptures.push_back(std::move(e));
		}
		for (const auto& r : p.flagReturnEvents)
		{
			WDLFlagEventV6 e;
			e.playerName = p.name;
			e.activatorX = r.ax;
			e.activatorY = r.ay;
			e.activatorZ = r.az;
			bucket(r.gameTic).flagReturns.push_back(std::move(e));
		}
	}

	// OrderBy(GameTic) — stable so within-tic insertion order is preserved.
	std::stable_sort(out.gameEvents.begin(), out.gameEvents.end(),
	                 [](const WDLGameEventsV6& a, const WDLGameEventsV6& b) {
		                 return a.gameTic < b.gameTic;
	                 });
}

WDLGameV6 WDLAggGame::Build(const WDLAggMeta& meta, const WDLItemSpawns& itemSpawns,
                            const WDLPlayerSpawns& playerSpawns,
                            const WDLFlagLocations& flagLocations,
                            const std::vector<WDLWadV6>& wads) const
{
	WDLGameV6 out;

	// Metadata.
	char mapbuf[16];
	std::snprintf(mapbuf, sizeof(mapbuf), "MAP%02d", meta.levelNum);
	out.metaData.parserVersion = meta.version;
	out.metaData.date = meta.date;
	out.metaData.mapNumber = mapbuf;
	out.metaData.mapName = meta.levelName;
	out.metaData.lives = meta.lives;
	out.metaData.gameType = meta.gameType;
	out.metaData.attackDefend = meta.attackDefend;
	out.metaData.originalLogFileName = meta.originalLogFileName;
	out.metaData.durationTics = meta.durationTics;
	out.metaData.durationSeconds = meta.durationTics / 35;
	out.metaData.round = meta.round;
	out.metaData.winResult = meta.winResult;
	out.metaData.winId = meta.winId;
	out.metaData.hostName = meta.hostName;

	// Map tables (translate recorder structs -> output views).
	out.wads = wads;
	for (const auto& is : itemSpawns)
		out.itemSpawns.push_back(
		    WDLItemSpawnsV6{is.id, is.x, is.y, is.z, static_cast<int>(is.item)});
	for (const auto& ps : playerSpawns)
		out.playerSpawns.push_back(
		    WDLPlayerSpawnsV6{ps.id, ps.x, ps.y, ps.z, static_cast<int>(ps.team)});
	for (const auto& fl : flagLocations)
		out.flagLocations.push_back(
		    WDLFlagLocationsV6{static_cast<int>(fl.team), fl.x, fl.y, fl.z});

	GenerateTeamStats(out);
	GenerateFlagAssistTable(out);
	GeneratePlayerStats(out);
	GenerateKillDeaths(out);
	GenerateGameEvents(out);

	return out;
}
