/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2008,2009 IITP RAS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Kirill Andreev <andreev@iitp.ru>
 */


#include "ns3/ie-dot11s-beacon-timing.h"
#include "ns3/log.h"
namespace ns3 {
namespace dot11s {
/*******************************************
 * IeDot11sBeaconTimingUnit
 *******************************************/
IeDot11sBeaconTimingUnit::IeDot11sBeaconTimingUnit ():
  m_aid (0),
  m_lastBeacon (0),
  m_beaconInterval (0)
{
}

void
IeDot11sBeaconTimingUnit::SetAid (uint8_t aid)
{
  m_aid = aid;
}

void
IeDot11sBeaconTimingUnit::SetLastBeacon (uint16_t lastBeacon)
{
  m_lastBeacon = lastBeacon;
}

void
IeDot11sBeaconTimingUnit::SetBeaconInterval (uint16_t beaconInterval)
{
  m_beaconInterval = beaconInterval;
}

uint8_t
IeDot11sBeaconTimingUnit::GetAid ()
{
  return m_aid;
}

uint16_t
IeDot11sBeaconTimingUnit::GetLastBeacon ()
{
  return m_lastBeacon;
}

uint16_t
IeDot11sBeaconTimingUnit::GetBeaconInterval ()
{
  return m_beaconInterval;
}

/*******************************************
 * IeDot11sBeaconTiming
 *******************************************/
IeDot11sBeaconTiming::IeDot11sBeaconTiming ():
  m_numOfUnits (0)
{
}

IeDot11sBeaconTiming::NeighboursTimingUnitsList
IeDot11sBeaconTiming::GetNeighboursTimingElementsList ()
{
  return m_neighbours;
}

void
IeDot11sBeaconTiming::AddNeighboursTimingElementUnit (
  uint16_t aid,
  Time  last_beacon, //MicroSeconds!
  Time  beacon_interval //MicroSeconds!
)
{
  if (m_numOfUnits == 50)
    return;
  //Firs we lookup if this element already exists
  for (NeighboursTimingUnitsList::iterator i = m_neighbours.begin (); i != m_neighbours.end(); i++)
    if (
      ((*i)->GetAid () == AidToU8(aid))
      && ((*i)->GetLastBeacon () == TimestampToU16(last_beacon))
      && ((*i)->GetBeaconInterval () == BeaconIntervalToU16(beacon_interval))
    )
      return;
  Ptr<IeDot11sBeaconTimingUnit>new_element = Create<IeDot11sBeaconTimingUnit> ();
  new_element->SetAid (AidToU8(aid));
  new_element->SetLastBeacon (TimestampToU16(last_beacon));
  new_element->SetBeaconInterval (BeaconIntervalToU16(beacon_interval));
  m_neighbours.push_back (new_element);
  m_numOfUnits++;
}

void
IeDot11sBeaconTiming::DelNeighboursTimingElementUnit (
  uint16_t aid,
  Time  last_beacon, //MicroSeconds!
  Time  beacon_interval //MicroSeconds!
)
{
  for (NeighboursTimingUnitsList::iterator i = m_neighbours.begin (); i != m_neighbours.end(); i++)
    if (
      ((*i)->GetAid () == AidToU8(aid))
      && ((*i)->GetLastBeacon () == TimestampToU16(last_beacon))
      && ((*i)->GetBeaconInterval () == BeaconIntervalToU16(beacon_interval))
    )
      {
        m_neighbours.erase (i);
        m_numOfUnits--;
        break;
      }
}

void
IeDot11sBeaconTiming::ClearTimingElement ()
{
  uint16_t to_delete = 0;
  uint16_t i;
  for (NeighboursTimingUnitsList::iterator j = m_neighbours.begin (); j != m_neighbours.end(); j++)
    {
      to_delete++;
      (*j) = 0;
    }
  for (i = 0; i < to_delete; i ++)
    m_neighbours.pop_back ();
  m_neighbours.clear ();

}

uint8_t
IeDot11sBeaconTiming::GetInformationSize () const
{
  return (5*m_numOfUnits);
}

void
IeDot11sBeaconTiming::PrintInformation (std::ostream& os) const
{
  for (NeighboursTimingUnitsList::const_iterator j = m_neighbours.begin (); j != m_neighbours.end(); j++)
    os << "AID=" << (*j)->GetAid () << ", Last beacon was at "
      << (*j)->GetLastBeacon ()<<", with beacon interval " << (*j)->GetBeaconInterval () << "\n";
}

void
IeDot11sBeaconTiming::SerializeInformation (Buffer::Iterator i) const
{
  for (NeighboursTimingUnitsList::const_iterator j = m_neighbours.begin (); j != m_neighbours.end(); j++)
  {
    i.WriteU8 ((*j)->GetAid ());
    i.WriteHtonU16 ((*j)->GetLastBeacon ());
    i.WriteHtonU16 ((*j)->GetBeaconInterval ());
  }
}
uint8_t 
IeDot11sBeaconTiming::DeserializeInformation (Buffer::Iterator start, uint8_t length)
{
  Buffer::Iterator i = start;
  m_numOfUnits = length/5;
  for (int j = 0; j < m_numOfUnits; j ++)
    {
      Ptr<IeDot11sBeaconTimingUnit> new_element = Create<IeDot11sBeaconTimingUnit> ();
      new_element->SetAid (i.ReadU8());
      new_element->SetLastBeacon (i.ReadNtohU16());
      new_element->SetBeaconInterval (i.ReadNtohU16());
      m_neighbours.push_back (new_element);
    }
  return i.GetDistanceFrom (start);
};

uint16_t
IeDot11sBeaconTiming::TimestampToU16 (Time x)
{
  return ((uint16_t) ((x.GetMicroSeconds() >> 8)&0xffff));
};

uint16_t 
IeDot11sBeaconTiming::BeaconIntervalToU16 (Time x)
{
  return ((uint16_t) (x.GetMicroSeconds() >>10)&0xffff);
};

uint8_t
IeDot11sBeaconTiming::AidToU8 (uint16_t x)
{
  return (uint8_t) (x&0xff);
};
  
} // namespace dot11s
} //namespace ns3
