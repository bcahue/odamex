// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id$
//
// Copyright (C) 1998-2006 by Randy Heit (ZDoom).
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
// Player statistics generation — WDLStats v6 JSON serialization + upload (S5).
//
//-----------------------------------------------------------------------------


#include "odamex.h"

#include "sv_stats.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <random>

#include "json/json.h"

#include "m_wdlstats_agg.h"
#include "sv_apiclient.h"

namespace
{

// Reproduce TimeSpan.FromSeconds(seconds).ToString("c") — System.Text.Json's
// default TimeSpan format, which the parity baseline uses. FromSeconds rounds to
// the nearest millisecond; "c" prints [-][d.]hh:mm:ss[.fffffff] with the 7-digit
// fraction present only when nonzero.
std::string FormatTimeSpan(double seconds)
{
	const long long ms =
	    static_cast<long long>(seconds * 1000.0 + (seconds >= 0 ? 0.5 : -0.5));
	const long long ticks = ms * 10000LL; // 100-ns ticks
	const bool neg = ticks < 0;
	long long t = neg ? -ticks : ticks;

	const long long TPS = 10000000LL;
	const long long TPM = 60LL * TPS;
	const long long TPH = 60LL * TPM;
	const long long TPD = 24LL * TPH;

	const long long days = t / TPD;
	t %= TPD;
	const long long hours = t / TPH;
	t %= TPH;
	const long long mins = t / TPM;
	t %= TPM;
	const long long secs = t / TPS;
	t %= TPS;
	const long long frac = t; // 0..9,999,999

	char buf[48];
	std::string out;
	if (neg)
		out += "-";
	if (days > 0)
		std::snprintf(buf, sizeof buf, "%lld.%02lld:%02lld:%02lld", days, hours, mins, secs);
	else
		std::snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld", hours, mins, secs);
	out += buf;
	if (frac != 0)
	{
		std::snprintf(buf, sizeof buf, ".%07lld", frac);
		out += buf;
	}
	return out;
}

// The recorder emits "...SSZ"; System.Text.Json serializes a UTC DateTimeOffset
// as "...SS+00:00". Match it so the date field diffs clean.
std::string FormatDateForStj(const std::string& iso)
{
	if (!iso.empty() && iso.back() == 'Z')
		return iso.substr(0, iso.size() - 1) + "+00:00";
	return iso;
}

// Server-minted match identity (RFC 4122 v4). Used for upload idempotency (§6.3).
std::string NewMatchGuid()
{
	std::random_device rd;
	std::mt19937_64 gen((static_cast<uint64_t>(rd()) << 32) ^ rd() ^
	                    static_cast<uint64_t>(::time(nullptr)));
	std::uniform_int_distribution<int> hex(0, 15);
	static const char* kHex = "0123456789abcdef";

	std::string s;
	s.reserve(36);
	for (int i = 0; i < 36; ++i)
	{
		if (i == 8 || i == 13 || i == 18 || i == 23)
			s += '-';
		else if (i == 14)
			s += '4'; // version 4
		else if (i == 19)
			s += kHex[8 + (hex(gen) & 3)]; // variant 10xx
		else
			s += kHex[hex(gen)];
	}
	return s;
}

template <class T, class F>
Json::Value JsonArrayOf(const std::vector<T>& v, F fn)
{
	Json::Value a(Json::arrayValue);
	for (const auto& x : v)
		a.append(fn(x));
	return a;
}

Json::Value U(unsigned x)
{
	return Json::Value(static_cast<Json::UInt>(x));
}

// --- event detail views ---

Json::Value ToJson(const WDLAccuracyEventV6& e)
{
	Json::Value j;
	j["shooterName"] = e.shooterName;
	j["targetName"] = e.targetName;
	j["weapon"] = e.weapon;
	j["shotsHit"] = U(e.shotsHit);
	j["maxShots"] = U(e.maxShots);
	j["spritePercent"] = e.spritePercent;
	j["pinpointPercent"] = e.pinpointPercent;
	j["angleBits"] = e.angleBits;
	j["activatorX"] = e.activatorX;
	j["activatorY"] = e.activatorY;
	j["activatorZ"] = e.activatorZ;
	j["targetX"] = e.targetX;
	j["targetY"] = e.targetY;
	j["targetZ"] = e.targetZ;
	return j;
}

Json::Value ToJson(const WDLDamageEventV6& e)
{
	Json::Value j;
	j["shooterName"] = e.shooterName;
	j["targetName"] = e.targetName;
	j["damageType"] = e.damageType;
	j["activatorX"] = e.activatorX;
	j["activatorY"] = e.activatorY;
	j["activatorZ"] = e.activatorZ;
	j["targetX"] = e.targetX;
	j["targetY"] = e.targetY;
	j["targetZ"] = e.targetZ;
	j["hp"] = e.hp;
	j["blueArmor"] = e.blueArmor;
	j["greenArmor"] = e.greenArmor;
	return j;
}

Json::Value ToJson(const WDLKillEventV6& e)
{
	Json::Value j;
	j["shooterName"] = e.shooterName;
	j["targetName"] = e.targetName;
	j["weapon"] = e.weapon;
	j["activatorX"] = e.activatorX;
	j["activatorY"] = e.activatorY;
	j["activatorZ"] = e.activatorZ;
	j["targetX"] = e.targetX;
	j["targetY"] = e.targetY;
	j["targetZ"] = e.targetZ;
	return j;
}

Json::Value ToJson(const WDLPickupEventV6& e)
{
	Json::Value j;
	j["playerName"] = e.playerName;
	j["type"] = e.type;
	j["activatorX"] = e.activatorX;
	j["activatorY"] = e.activatorY;
	j["activatorZ"] = e.activatorZ;
	return j;
}

Json::Value ToJson(const WDLFlagEventV6& e)
{
	Json::Value j;
	j["playerName"] = e.playerName;
	j["activatorX"] = e.activatorX;
	j["activatorY"] = e.activatorY;
	j["activatorZ"] = e.activatorZ;
	return j;
}

Json::Value ToJson(const WDLGameEventsV6& e)
{
	Json::Value j;
	j["gameTic"] = e.gameTic;
	j["accuracyEvents"] = JsonArrayOf(e.accuracyEvents, [](const WDLAccuracyEventV6& x) { return ToJson(x); });
	j["kills"] = JsonArrayOf(e.kills, [](const WDLKillEventV6& x) { return ToJson(x); });
	j["carrierKills"] = JsonArrayOf(e.carrierKills, [](const WDLKillEventV6& x) { return ToJson(x); });
	j["suicides"] = JsonArrayOf(e.suicides, [](const WDLKillEventV6& x) { return ToJson(x); });
	j["suicidesWithFlag"] = JsonArrayOf(e.suicidesWithFlag, [](const WDLKillEventV6& x) { return ToJson(x); });
	j["environmentalDeaths"] = JsonArrayOf(e.environmentalDeaths, [](const WDLKillEventV6& x) { return ToJson(x); });
	j["environmentalDeathsWithFlag"] = JsonArrayOf(e.environmentalDeathsWithFlag, [](const WDLKillEventV6& x) { return ToJson(x); });
	j["damage"] = JsonArrayOf(e.damage, [](const WDLDamageEventV6& x) { return ToJson(x); });
	j["selfDamage"] = JsonArrayOf(e.selfDamage, [](const WDLDamageEventV6& x) { return ToJson(x); });
	j["selfDamageWithFlag"] = JsonArrayOf(e.selfDamageWithFlag, [](const WDLDamageEventV6& x) { return ToJson(x); });
	j["environmentalDamage"] = JsonArrayOf(e.environmentalDamage, [](const WDLDamageEventV6& x) { return ToJson(x); });
	j["environmentalDamageWithFlag"] = JsonArrayOf(e.environmentalDamageWithFlag, [](const WDLDamageEventV6& x) { return ToJson(x); });
	j["pickups"] = JsonArrayOf(e.pickups, [](const WDLPickupEventV6& x) { return ToJson(x); });
	j["pickupFlagTouches"] = JsonArrayOf(e.pickupFlagTouches, [](const WDLFlagEventV6& x) { return ToJson(x); });
	j["flagTouches"] = JsonArrayOf(e.flagTouches, [](const WDLFlagEventV6& x) { return ToJson(x); });
	j["flagCaptures"] = JsonArrayOf(e.flagCaptures, [](const WDLFlagEventV6& x) { return ToJson(x); });
	j["flagReturns"] = JsonArrayOf(e.flagReturns, [](const WDLFlagEventV6& x) { return ToJson(x); });
	return j;
}

// --- aggregates ---

Json::Value ToJson(const WDLDamageAggregateV6& a)
{
	Json::Value j;
	j["totalDamage"] = a.totalDamage;
	j["totalDamageGreenArmor"] = a.totalDamageGreenArmor;
	j["totalDamageBlueArmor"] = a.totalDamageBlueArmor;
	j["targetName"] = a.targetName;
	j["targetId"] = a.targetId;
	j["weapon"] = a.weapon;
	return j;
}

Json::Value ToJson(const WDLKillAggregateV6& a)
{
	Json::Value j;
	j["totalKills"] = a.totalKills;
	j["targetName"] = a.targetName;
	j["targetId"] = a.targetId;
	j["weapon"] = a.weapon;
	return j;
}

Json::Value ToJson(const WDLAccuracyAggregateV6& a)
{
	Json::Value j;
	j["pinpointPercentage"] = a.pinpointPercentage;
	j["spritePercentage"] = a.spritePercentage;
	j["totalShotsHit"] = U(a.totalShotsHit);
	j["totalShotsMissed"] = U(a.totalShotsMissed);
	j["totalShotsAttempted"] = U(a.totalShotsAttempted);
	j["totalPelletsHit"] = U(a.totalPelletsHit);
	j["totalPelletsMissed"] = U(a.totalPelletsMissed);
	j["totalPelletsAttempted"] = U(a.totalPelletsAttempted);
	j["hitMissRatio"] = a.hitMissRatio;
	j["weapon"] = a.weapon;
	return j;
}

Json::Value ToJson(const WDLPickupAggregateV6& a)
{
	Json::Value j;
	j["pickupType"] = a.pickupType;
	j["totalPickups"] = a.totalPickups;
	return j;
}

// --- player / team / kill-death / flag-assist ---

Json::Value ToJson(const WDLPlayerStatsV6& p)
{
	Json::Value j;
	j["id"] = p.id;
	j["netId"] = p.netId;
	j["name"] = p.name;
	j["sub"] = p.sub;
	j["team"] = p.team;
	j["assists"] = p.assists;
	j["captures"] = p.captures;
	j["pickupCaptures"] = p.pickupCaptures;
	j["capturesWithSuperPickup"] = p.capturesWithSuperPickup;
	j["flagTouches"] = p.flagTouches;
	j["pickupFlagTouches"] = p.pickupFlagTouches;
	j["damageOutputBetweenTouchAndCaptureMin"] = p.damageOutputBetweenTouchAndCaptureMin;
	j["damageOutputBetweenTouchAndCaptureMax"] = p.damageOutputBetweenTouchAndCaptureMax;
	j["damageOutputBetweenTouchAndCaptureAvg"] = p.damageOutputBetweenTouchAndCaptureAvg;
	j["captureTimeMin"] = FormatTimeSpan(p.captureTimeMin);
	j["captureTimeMax"] = FormatTimeSpan(p.captureTimeMax);
	j["captureTimeAvg"] = FormatTimeSpan(p.captureTimeAvg);
	j["captureTimeMinTics"] = p.captureTimeMinTics;
	j["captureTimeMaxTics"] = p.captureTimeMaxTics;
	j["captureTimeAvgTics"] = p.captureTimeAvgTics;
	j["captureHealthMin"] = p.captureHealthMin;
	j["captureHealthMax"] = p.captureHealthMax;
	j["captureHealthAvg"] = p.captureHealthAvg;
	j["captureGreenArmorMin"] = p.captureGreenArmorMin;
	j["captureGreenArmorMax"] = p.captureGreenArmorMax;
	j["captureGreenArmorAvg"] = p.captureGreenArmorAvg;
	j["captureBlueArmorMin"] = p.captureBlueArmorMin;
	j["captureBlueArmorMax"] = p.captureBlueArmorMax;
	j["captureBlueArmorAvg"] = p.captureBlueArmorAvg;
	j["flagCarriersKilledWhileHoldingFlag"] = p.flagCarriersKilledWhileHoldingFlag;
	j["highestKillsBeforeCapturing"] = p.highestKillsBeforeCapturing;
	j["pickupCaptureTimeMin"] = FormatTimeSpan(p.pickupCaptureTimeMin);
	j["pickupCaptureTimeMax"] = FormatTimeSpan(p.pickupCaptureTimeMax);
	j["pickupCaptureTimeAvg"] = FormatTimeSpan(p.pickupCaptureTimeAvg);
	j["pickupCaptureTimeMinTics"] = p.pickupCaptureTimeMinTics;
	j["pickupCaptureTimeMaxTics"] = p.pickupCaptureTimeMaxTics;
	j["pickupCaptureTimeAvgTics"] = p.pickupCaptureTimeAvgTics;
	j["totalDamage"] = p.totalDamage;
	j["totalTeamDamage"] = p.totalTeamDamage;
	j["selfDamage"] = p.selfDamage;
	j["selfDamageWithFlag"] = p.selfDamageWithFlag;
	j["totalGreenArmorDamage"] = p.totalGreenArmorDamage;
	j["totalBlueArmorDamage"] = p.totalBlueArmorDamage;
	j["totalDamageToFlagCarriers"] = p.totalDamageToFlagCarriers;
	j["totalDamageAsFlagCarrier"] = p.totalDamageAsFlagCarrier;
	j["totalDamageToFlagCarriersWhileHoldingFlag"] = p.totalDamageToFlagCarriersWhileHoldingFlag;
	j["totalDamageTakenFromEnvironment"] = p.totalDamageTakenFromEnvironment;
	j["totalDamageTakenFromEnvironmentAsFlagCarrier"] = p.totalDamageTakenFromEnvironmentAsFlagCarrier;
	j["totalFlagReturns"] = p.totalFlagReturns;
	j["totalKills"] = p.totalKills;
	j["killsWithFlag"] = p.killsWithFlag;
	j["killDeathRatio"] = p.killDeathRatio;
	j["capturePercentage"] = p.capturePercentage;
	j["pickupCapturePercentage"] = p.pickupCapturePercentage;
	j["overallCapturePercentage"] = p.overallCapturePercentage;
	j["flagDefenses"] = p.flagDefenses;
	j["totalDeaths"] = p.totalDeaths;
	j["deaths"] = p.deaths;
	j["flagCarrierDeaths"] = p.flagCarrierDeaths;
	j["suicides"] = p.suicides;
	j["suicidesWithFlag"] = p.suicidesWithFlag;
	j["environmentalDeaths"] = p.environmentalDeaths;
	j["environmentalDeathsAsFlagCarrier"] = p.environmentalDeathsAsFlagCarrier;
	j["teamKills"] = p.teamKills;
	j["longestSpree"] = p.longestSpree;
	j["highestMultiKill"] = p.highestMultiKill;
	j["totalPowerPickups"] = p.totalPowerPickups;
	j["totalHealthFromPickups"] = p.totalHealthFromPickups;
	j["healthFromNonPowerPickups"] = p.healthFromNonPowerPickups;
	j["healthFromPowerPickups"] = p.healthFromPowerPickups;
	j["touchHealthMin"] = p.touchHealthMin;
	j["touchHealthMax"] = p.touchHealthMax;
	j["touchHealthAvg"] = p.touchHealthAvg;
	j["touchGreenArmorMin"] = p.touchGreenArmorMin;
	j["touchGreenArmorMax"] = p.touchGreenArmorMax;
	j["touchGreenArmorAvg"] = p.touchGreenArmorAvg;
	j["touchBlueArmorMin"] = p.touchBlueArmorMin;
	j["touchBlueArmorMax"] = p.touchBlueArmorMax;
	j["touchBlueArmorAvg"] = p.touchBlueArmorAvg;
	j["touchHealthResultCaptureMin"] = p.touchHealthResultCaptureMin;
	j["touchHealthResultCaptureMax"] = p.touchHealthResultCaptureMax;
	j["touchHealthResultCaptureAvg"] = p.touchHealthResultCaptureAvg;
	j["touchesOverOneHundredHealth"] = p.touchesOverOneHundredHealth;
	j["damageList"] = JsonArrayOf(p.damageList, [](const WDLDamageAggregateV6& x) { return ToJson(x); });
	j["damageWithFlagList"] = JsonArrayOf(p.damageWithFlagList, [](const WDLDamageAggregateV6& x) { return ToJson(x); });
	j["damageToFlagCarriersList"] = JsonArrayOf(p.damageToFlagCarriersList, [](const WDLDamageAggregateV6& x) { return ToJson(x); });
	j["damageToFlagCarriersWithFlagList"] = JsonArrayOf(p.damageToFlagCarriersWithFlagList, [](const WDLDamageAggregateV6& x) { return ToJson(x); });
	j["killsList"] = JsonArrayOf(p.killsList, [](const WDLKillAggregateV6& x) { return ToJson(x); });
	j["killsWithFlagList"] = JsonArrayOf(p.killsWithFlagList, [](const WDLKillAggregateV6& x) { return ToJson(x); });
	j["carrierKillList"] = JsonArrayOf(p.carrierKillList, [](const WDLKillAggregateV6& x) { return ToJson(x); });
	j["carrierKillsWhileHoldingFlagList"] = JsonArrayOf(p.carrierKillsWhileHoldingFlagList, [](const WDLKillAggregateV6& x) { return ToJson(x); });
	j["accuracyList"] = JsonArrayOf(p.accuracyList, [](const WDLAccuracyAggregateV6& x) { return ToJson(x); });
	j["accuracyWithFlagList"] = JsonArrayOf(p.accuracyWithFlagList, [](const WDLAccuracyAggregateV6& x) { return ToJson(x); });
	j["accuracyWithoutFlagList"] = JsonArrayOf(p.accuracyWithoutFlagList, [](const WDLAccuracyAggregateV6& x) { return ToJson(x); });
	j["pickupList"] = JsonArrayOf(p.pickupList, [](const WDLPickupAggregateV6& x) { return ToJson(x); });
	return j;
}

Json::Value ToJson(const WDLTeamStatsV6& t)
{
	Json::Value j;
	j["points"] = t.points;
	j["captures"] = t.captures;
	j["pickupCaptures"] = t.pickupCaptures;
	j["assists"] = t.assists;
	j["flagTouches"] = t.flagTouches;
	j["pickupFlagTouches"] = t.pickupFlagTouches;
	j["totalCapturePercentage"] = t.totalCapturePercentage;
	j["pickupCapturePercentage"] = t.pickupCapturePercentage;
	j["capturePercentage"] = t.capturePercentage;
	j["frags"] = t.frags;
	j["deaths"] = t.deaths;
	j["killDeathRatio"] = t.killDeathRatio;
	j["damage"] = t.damage;
	j["flagDefenses"] = t.flagDefenses;
	j["powerPickups"] = t.powerPickups;
	j["teamPlayers"] = JsonArrayOf(t.teamPlayers, [](const std::string& s) { return Json::Value(s); });
	return j;
}

Json::Value ToJson(const WDLKillDeathEventV6& k)
{
	Json::Value j;
	j["killerName"] = k.killerName;
	j["killerId"] = k.killerId;
	j["killerX"] = k.killerX;
	j["killerY"] = k.killerY;
	j["killerZ"] = k.killerZ;
	j["targetName"] = k.targetName;
	j["targetId"] = k.targetId;
	j["targetX"] = k.targetX;
	j["targetY"] = k.targetY;
	j["targetZ"] = k.targetZ;
	j["weapon"] = k.weapon;
	return j;
}

Json::Value ToJson(const WDLFlagAssistDataV6& a)
{
	Json::Value j;
	j["flagTouchTimeTics"] = a.flagTouchTimeTics;
	j["flagTouchTime"] = FormatTimeSpan(a.flagTouchTime);
	j["playerId"] = a.playerId;
	j["playerName"] = a.playerName;
	return j;
}

Json::Value ToJson(const WDLFlagTouchCapturesV6& c)
{
	Json::Value j;
	j["timeCapturedTics"] = c.timeCapturedTics;
	j["timeCaptured"] = FormatTimeSpan(c.timeCaptured);
	j["team"] = c.team;
	j["flagAssists"] = JsonArrayOf(c.flagAssists, [](const WDLFlagAssistDataV6& x) { return ToJson(x); });
	return j;
}

Json::Value ToJson(const WDLFlagAssistTableV6& f)
{
	Json::Value j;
	j["flagTouchCaptures"] = JsonArrayOf(f.flagTouchCaptures, [](const WDLFlagTouchCapturesV6& x) { return ToJson(x); });
	return j;
}

// --- metadata + map entities ---

Json::Value ToJson(const WDLGameMetaDataV6& m)
{
	Json::Value j;
	j["parserVersion"] = m.parserVersion;
	j["date"] = FormatDateForStj(m.date);
	j["mapNumber"] = m.mapNumber;
	j["mapName"] = m.mapName;
	j["lives"] = m.lives;
	j["gameType"] = m.gameType;
	j["attackDefend"] = m.attackDefend;
	j["originalLogFileName"] = m.originalLogFileName;
	j["durationTics"] = m.durationTics;
	j["duration"] = FormatTimeSpan(m.durationSeconds);
	j["round"] = m.round;
	j["winResult"] = m.winResult;
	j["winId"] = m.winId;
	j["hostName"] = m.hostName;
	return j;
}

Json::Value ToJson(const WDLWadV6& w)
{
	Json::Value j;
	j["filename"] = w.filename;
	j["hash"] = w.hash;
	return j;
}

Json::Value ToJson(const WDLItemSpawnsV6& s)
{
	Json::Value j;
	j["id"] = s.id;
	j["x"] = s.x;
	j["y"] = s.y;
	j["z"] = s.z;
	j["pickup"] = s.pickup;
	return j;
}

Json::Value ToJson(const WDLPlayerSpawnsV6& s)
{
	Json::Value j;
	j["id"] = s.id;
	j["x"] = s.x;
	j["y"] = s.y;
	j["z"] = s.z;
	j["team"] = s.team;
	return j;
}

Json::Value ToJson(const WDLFlagLocationsV6& f)
{
	Json::Value j;
	j["team"] = f.team;
	j["x"] = f.x;
	j["y"] = f.y;
	j["z"] = f.z;
	return j;
}

Json::Value ToJson(const WDLGameV6& g)
{
	Json::Value j;
	j["metaData"] = ToJson(g.metaData);
	j["wads"] = JsonArrayOf(g.wads, [](const WDLWadV6& x) { return ToJson(x); });
	j["itemSpawns"] = JsonArrayOf(g.itemSpawns, [](const WDLItemSpawnsV6& x) { return ToJson(x); });
	j["playerSpawns"] = JsonArrayOf(g.playerSpawns, [](const WDLPlayerSpawnsV6& x) { return ToJson(x); });
	j["redTeamStats"] = ToJson(g.redTeamStats);
	j["blueTeamStats"] = ToJson(g.blueTeamStats);
	j["greenTeamStats"] = ToJson(g.greenTeamStats);
	j["flagAssistTable"] = ToJson(g.flagAssistTable);
	j["playerStats"] = JsonArrayOf(g.playerStats, [](const WDLPlayerStatsV6& x) { return ToJson(x); });
	j["playerKillDeath"] = JsonArrayOf(g.playerKillDeath, [](const WDLKillDeathEventV6& x) { return ToJson(x); });
	j["playerFlagCarrierKillDeath"] = JsonArrayOf(g.playerFlagCarrierKillDeath, [](const WDLKillDeathEventV6& x) { return ToJson(x); });
	j["flagLocations"] = JsonArrayOf(g.flagLocations, [](const WDLFlagLocationsV6& x) { return ToJson(x); });
	j["gameEvents"] = JsonArrayOf(g.gameEvents, [](const WDLGameEventsV6& x) { return ToJson(x); });
	return j;
}

} // namespace

std::string SV_SerializeMatchStats()
{
	const WDLGameV6 game = M_BuildWDLGameV6();

	// Nothing recorded — don't emit an empty match.
	if (game.playerStats.empty() && game.gameEvents.empty())
		return std::string();

	Json::Value root = ToJson(game);
	// Server-minted match identity for upload idempotency (§6.3).
	root["matchId"] = NewMatchGuid();

	Json::StreamWriterBuilder writer;
	writer["indentation"] = "";
	return Json::writeString(writer, root);
}

void SV_UploadMatchStats()
{
	// TODO(S6): wire this into the match-end / intermission path and upload off
	// the event worker thread. For now it is not called from anywhere.
	std::string blob = SV_SerializeMatchStats();
	if (blob.empty())
		return;

	SV_ApiUploadMatchStats(blob);
}

VERSION_CONTROL (sv_stats_cpp, "$Id$")
