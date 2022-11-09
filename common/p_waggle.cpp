// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id$
//
// Copyright (C) 1998-2006 by Randy Heit (ZDoom).
// Copyright (C) 2006-2022 by The Odamex Team.
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
//   [Blair] Waggle is handled differently than other floor types.
//   We simply send Waggle start and waggle stop, and the tic when they started
//   or stopped. Sector height isn't sent every tic, but snapshots are made
//   on both client and server.
//
//-----------------------------------------------------------------------------

#include "odamex.h"

#include "p_local.h"

static const fixed_t FloatBobOffsets[64] = {
    0,       51389,   102283,  152192,  200636,  247147,  291278,  332604,
    370727,  405280,  435929,  462380,  484378,  501712,  514213,  521763,
    524287,  521763,  514213,  501712,  484378,  462380,  435929,  405280,
    370727,  332604,  291278,  247147,  200636,  152192,  102283,  51389,
    -1,      -51390,  -102284, -152193, -200637, -247148, -291279, -332605,
    -370728, -405281, -435930, -462381, -484380, -501713, -514215, -521764,
    -524288, -521764, -514214, -501713, -484379, -462381, -435930, -405280,
    -370728, -332605, -291279, -247148, -200637, -152193, -102284, -51389};

bool EV_StartPlaneWaggle(int tag, line_t* line, int height, int speed, int offset,
                             int timer, bool ceiling)
{
    int sectorIndex;
    sector_t* sector;
    bool retCode;

    retCode = false;
    sectorIndex = -1;
    while ((sectorIndex = P_FindSectorFromTagOrLine(tag, line, sectorIndex)) >= 0)
    {
        sector = &sectors[sectorIndex];
        if (ceiling ? P_CeilingActive(sector) : P_FloorActive(sector))
        { // Already busy with another thinker
            continue;
        }
        retCode = true;
        new DWaggle(sector, height, speed, offset, timer, ceiling);

        if (ceiling)
        {
            P_AddMovingCeiling(sector);
        }
        else
        {
            P_AddMovingFloor(sector);
        }
    }

    return retCode;
}

IMPLEMENT_SERIAL(DWaggle, DMover)

void DWaggle::Serialize(FArchive& arc)
{
    Super::Serialize (arc);
    if (arc.IsStoring ())
    {
        arc << m_OriginalHeight
            << m_Accumulator
            << m_AccDelta
            << m_TargetScale
            << m_Scale
            << m_ScaleDelta
            << m_Ticker
            << m_State
            << m_Ceiling
            << m_StartTic;
    }
    else
    {
        arc >> m_OriginalHeight
            >> m_Accumulator
            >> m_AccDelta
            >> m_TargetScale
            >> m_Scale
            >> m_ScaleDelta
            >> m_Ticker
            >> m_State
            >> m_Ceiling
            >> m_StartTic;
    }
}

DWaggle::DWaggle()
{

}

DWaggle::DWaggle(sector_t* sector, int height, int speed, int offset, int timer,
                 bool ceiling)
{
    if (ceiling)
    {
        sector->ceilingdata = this;
        m_OriginalHeight = sector->ceilingheight;
    }
    else
    {
        sector->floordata = this;
        m_OriginalHeight = sector->floorheight;
    }
    m_Sector = sector;
    m_Accumulator = offset * FRACUNIT;
    m_AccDelta = speed << 10;
    m_Scale = 0;
    m_TargetScale = height << 10;
    m_ScaleDelta = m_TargetScale / (35 + ((3 * 35) * height) / 255);
    m_Ticker = timer ? timer * 35 : -1;
    m_State = init;
    m_Ceiling = ceiling;
    m_StartTic = ::level.time; // [Blair] Used for client side synchronization
}

void DWaggle::RunThink()
{
    switch (m_State)
    {
    case finished:
        Destroy();
        m_State = destroy;
    case destroy:
        return;
        break;
    case init:
        m_State = expand;
        // fall thru
    case expand:
        if ((m_Scale += m_ScaleDelta) >= m_TargetScale)
        {
            m_Scale = m_TargetScale;
            m_State = stable;
        }
        break;
    case reduce:
        if ((m_Scale -= m_ScaleDelta) <= 0)
        { // Remove
            if (m_Ceiling)
            {
                P_SetCeilingHeight(m_Sector, m_OriginalHeight);
            }
            else
            {
                P_SetFloorHeight(m_Sector, m_OriginalHeight);
            }
            P_ChangeSector(m_Sector, DOOM_CRUSH);
            m_State = finished;
            return;
        }
        break;
    case stable:
        if (m_Ticker != -1)
        {
            if (!--m_Ticker)
            {
                m_State = reduce;
            }
        }
        break;
    }

    m_Accumulator += m_AccDelta;
    fixed_t changeamount = m_OriginalHeight + FixedMul(FloatBobOffsets[(m_Accumulator >> FRACBITS) & 63], m_Scale);

    if (m_Ceiling)
    {
        P_SetCeilingHeight(m_Sector, changeamount);
    }
    else
    {
        P_SetFloorHeight(m_Sector, changeamount);
    }
    P_ChangeSector(m_Sector, DOOM_CRUSH);
}

DWaggle::DWaggle(sector_t* sec) : Super(sec) { }

// Clones a DWaggle and returns a pointer to that clone.
//
// The caller owns the pointer, and it must be deleted with `delete`.
DWaggle* DWaggle::Clone(sector_t* sec) const
{
    DWaggle* wagg = new DWaggle(*this);

    wagg->Orphan();
	  wagg->m_Sector = sec;

    return wagg;
}
