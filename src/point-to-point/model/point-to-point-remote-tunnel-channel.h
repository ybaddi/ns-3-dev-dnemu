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

#ifndef POINT_TO_POINT_REMOTE_TUNNEL_CHANNEL_H
#define POINT_TO_POINT_REMOTE_TUNNEL_CHANNEL_H

#include "point-to-point-channel.h"
#include "ns3/realtime-simulator-impl.h"
#include <arpa/inet.h>

namespace ns3 {

/**
 * \ingroup point-to-point
 */
class PointToPointRemoteTunnelChannel : public PointToPointChannel
{
public:
  static TypeId GetTypeId (void);
  PointToPointRemoteTunnelChannel ();
  ~PointToPointRemoteTunnelChannel ();
  void SetDestination (int systemId);
  void Receive (void);
  virtual bool TransmitStart (Ptr<Packet> p, Ptr<PointToPointNetDevice> src, Time txTime);
private:
  struct sockaddr_in m_dst;
  RealtimeSimulatorImpl *m_rtImpl;
};
}

#endif


