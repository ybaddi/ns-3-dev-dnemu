/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2012 NICT
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
 * Author: Hajime Tazaki <tazaki@nict.go.jp>
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#include "point-to-point-remote-tunnel-channel.h"
#include "point-to-point-net-device.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include "ns3/mpi-interface.h"
#include "ns3/node-list.h"


using namespace std;

NS_LOG_COMPONENT_DEFINE ("PointToPointRemoteTunnelChannel");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (PointToPointRemoteTunnelChannel);

TypeId
PointToPointRemoteTunnelChannel::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PointToPointRemoteTunnelChannel")
    .SetParent<PointToPointChannel> ()
    .AddConstructor<PointToPointRemoteTunnelChannel> ()
  ;
  return tid;
}

static uint32_t m_txCount = 0;
static uint32_t m_rxCount = 0;

PointToPointRemoteTunnelChannel::PointToPointRemoteTunnelChannel ()
{
  NS_LOG_FUNCTION (this);
  return;
  // FIXME
  Ptr<RealtimeSimulatorImpl> impl = DynamicCast<RealtimeSimulatorImpl> (Simulator::GetImplementation ());
  
  m_rtImpl = GetPointer (impl);
  m_rtImpl->ScheduleRealtimeNow (MakeEvent (&PointToPointRemoteTunnelChannel::Receive, this));
}

PointToPointRemoteTunnelChannel::~PointToPointRemoteTunnelChannel ()
{
  close (MpiInterface::GetDistSocket ());
  NS_LOG_INFO ("PtP Tx = " << m_txCount);
  NS_LOG_INFO ("PtP Rx = " << m_rxCount << " dst to " << inet_ntoa (m_dst.sin_addr)
               << ":" << htons (m_dst.sin_port));
}

void
PointToPointRemoteTunnelChannel::SetDestination (int systemId)
{
  NS_LOG_FUNCTION (this);
  struct sockaddr_in *dst_sockaddr;

  dst_sockaddr = MpiInterface::GetSockAddress (systemId);
  memcpy (&m_dst, dst_sockaddr, sizeof (*dst_sockaddr));
}


void
PointToPointRemoteTunnelChannel::Receive (void)
{
  NS_LOG_FUNCTION (this);
  int ret;
  char buffer [1500];
  struct timeval timeout;
  fd_set fdset;

  memset (&timeout, 0, sizeof (struct timeval));
  FD_ZERO (&fdset);
  FD_SET (MpiInterface::GetDistSocket (), &fdset);
  ret = select (MpiInterface::GetDistSocket () + 1, &fdset, NULL, NULL, &timeout);
  if (ret < 0)
    {
      NS_LOG_ERROR ("select error " << strerror (errno) << "(" << errno << ")");
      NS_ASSERT (0);
      Simulator::Schedule (Seconds (0.1),
                           &PointToPointRemoteTunnelChannel::Receive, this);
      return;
    }
  else if (ret == 0)
    {
      NS_LOG_WARN ("select timedout");
      Simulator::Schedule (MilliSeconds (1),
                           &PointToPointRemoteTunnelChannel::Receive, this);
      return;
    }

  if (FD_ISSET (MpiInterface::GetDistSocket (), &fdset))
    {
      ret = recv (MpiInterface::GetDistSocket (), buffer, sizeof (buffer), MSG_DONTWAIT);
      if (ret < 0)
        {
          NS_ASSERT (0);
          NS_LOG_ERROR ("recv error " << strerror (errno) << "(" << errno << ")");
          Simulator::Schedule (Seconds (0.1),
                               &PointToPointRemoteTunnelChannel::Receive, this);
          return;
        }
    }

  m_rxCount++;
  NS_LOG_DEBUG ("recv succeed");


  uint32_t *pData = reinterpret_cast<uint32_t *> (buffer);
  uint32_t nodeId = *pData++;
  uint32_t devIdx  = *pData++;

  Ptr<Packet> p = Create<Packet> (reinterpret_cast<uint8_t *> (pData), 
                                  ret - (sizeof (uint32_t) * 2), true);

  Ptr<PointToPointNetDevice> dev = DynamicCast<PointToPointNetDevice> (NodeList::GetNode (nodeId)->GetDevice (devIdx));
  NS_LOG_DEBUG ("nodeid is " << nodeId);
  NS_LOG_DEBUG ("netdev is " << devIdx);
  NS_LOG_DEBUG ("packet is " << p);

  m_rtImpl->ScheduleRealtimeNowWithContext (nodeId,
                                  MakeEvent (&PointToPointNetDevice::Receive,
                                             dev, p));

  Simulator::Schedule (Seconds (0),
                       &PointToPointRemoteTunnelChannel::Receive, this);
}

bool
PointToPointRemoteTunnelChannel::TransmitStart (
  Ptr<Packet> p,
  Ptr<PointToPointNetDevice> src,
  Time txTime)
{
  NS_LOG_FUNCTION (this << p << src);
  NS_LOG_LOGIC ("Send to tun_dst " << inet_ntoa (m_dst.sin_addr) <<  ":" << ntohs(m_dst.sin_port) <<")");

  IsInitialized ();

  uint32_t wire = src == GetSource (0) ? 0 : 1;
  Ptr<PointToPointNetDevice> dst = GetDestination (wire);

  uint32_t serializedSize = p->GetSerializedSize ();
  uint8_t* buffer =  new uint8_t[serializedSize + 8];
  uint32_t* pData = reinterpret_cast <uint32_t *> (buffer);
  *pData++ = dst->GetNode ()->GetId ();
  *pData++ = dst->GetIfIndex ();
  p->Serialize (reinterpret_cast<uint8_t *> (pData), serializedSize);

  int ret;
  ret = sendto (MpiInterface::GetDistSocket (), buffer, serializedSize + 8, MSG_DONTWAIT,
          (struct sockaddr *)&m_dst, sizeof(m_dst));
  if (ret < 0)
    {
      NS_LOG_WARN ("sendto error " << errno);
      return false;
    }

  m_txCount++;
  // Call the tx anim callback on the net device
  //  m_txrxPointToPoint (p, src, GetDestination (wire), txTime, txTime);
  return true;
}

} // namespace ns3
