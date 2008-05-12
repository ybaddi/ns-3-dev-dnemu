/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
 */

//
// Network topology
//
//           10Mb/s, 10ms       10Mb/s, 10ms
//       n0-----------------n1-----------------n2
//
//
// - Tracing of queues and packet receptions to file 
//   "tcp-large-transfer.tr"
// - pcap traces also generated in the following files
//   "tcp-large-transfer.pcap-$n-$i" where n and i represent node and interface numbers respectively
//  Usage (e.g.): ./waf --run tcp-large-transfer

//XXX this isn't working as described right now
//it is just blasting away for 10 seconds, with no fixed amount of data
//being sent

#include <ctype.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cassert>

#include "ns3/core-module.h"
#include "ns3/helper-module.h"
#include "ns3/node-module.h"
#include "ns3/global-route-manager.h"
#include "ns3/simulator-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("TcpLargeTransfer");

void 
ApplicationTraceSink (Ptr<const Packet> packet,
                      const Address &addr)
{
// g_log is not declared in optimized builds
// should convert this to use of some other flag than the logging system
#ifdef NS3_LOG_ENABLE
  if (!g_log.IsNoneEnabled ()) {
    if (InetSocketAddress::IsMatchingType (addr) )
      {
      InetSocketAddress address = InetSocketAddress::ConvertFrom (addr);
        std::cout << "PacketSink received size " << 
        packet->GetSize () << " at time " << 
        Simulator::Now ().GetSeconds () << " from address: " << 
        address.GetIpv4 () << std::endl;
        char buf[2000]; 
        memcpy(buf, packet->PeekData (), packet->GetSize ());
        for (uint32_t i=0; i < packet->GetSize (); i++)
          {
            std::cout << buf[i];
            if (i && i % 60 == 0) 
              std::cout << std::endl; 
          }
        std::cout << std::endl << std::endl;
    }
  }
#endif
}

void CloseConnection (Ptr<Socket> localSocket);
void StartFlow(Ptr<Socket>, uint32_t, Ipv4Address, uint16_t);
void WriteUntilBufferFull (Ptr<Socket>, uint32_t);

int main (int argc, char *argv[])
{

  // Users may find it convenient to turn on explicit debugging
  // for selected modules; the below lines suggest how to do this
  //  LogComponentEnable("TcpL4Protocol", LOG_LEVEL_ALL);
  //  LogComponentEnable("TcpSocket", LOG_LEVEL_ALL);
  //  LogComponentEnable("PacketSink", LOG_LEVEL_ALL);
  //  LogComponentEnable("TcpLargeTransfer", LOG_LEVEL_ALL);

  //
  // Make the random number generators generate reproducible results.
  //
  RandomVariable::UseGlobalSeed (1, 1, 2, 3, 5, 8);

  // Allow the user to override any of the defaults and the above
  // Bind()s at run-time, via command-line arguments
  CommandLine cmd;
  cmd.Parse (argc, argv);

  // Here, we will explicitly create three nodes. 
  NodeContainer c0;
  c0.Create (2);

  NodeContainer c1;
  c1.Add (c0.Get (1));
  c1.Create (1);

  // We create the channels first without any IP addressing information
  PointToPointHelper p2p;
  p2p.SetChannelParameter ("BitRate", DataRateValue (DataRate(100000)));
  p2p.SetChannelParameter ("Delay", TimeValue (MilliSeconds(10)));
  NetDeviceContainer dev0 = p2p.Install (c0);
  NetDeviceContainer dev1 = p2p.Install (c1);

  // add ip/tcp stack to nodes.
  NodeContainer c = NodeContainer (c0, c1.Get (1));
  InternetStackHelper internet;
  internet.Install (c);

  // Later, we add IP addresses.  
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.3.0", "255.255.255.0");
  ipv4.Assign (dev0);
  ipv4.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer ipInterfs = ipv4.Assign (dev1);

  // and setup ip routing tables to get total ip-level connectivity.
  GlobalRouteManager::PopulateRoutingTables ();

  ///////////////////////////////////////////////////////////////////////////
  // Simulation 1
  // 
  // Send 2000000 bytes over a connection to server port 50000 at time 0
  // Should observe SYN exchange, a lot of data segments, and FIN exchange
  //
  ///////////////////////////////////////////////////////////////////////////

  int nBytes = 2000000;
  uint16_t servPort = 50000;

  // Create a packet sink to receive these packets
  PacketSinkHelper sink ("ns3::Tcp",
                         InetSocketAddress (Ipv4Address::GetAny (), servPort));

  ApplicationContainer apps = sink.Install (c1.Get (1));
  apps.Start (Seconds (0.0));

  // and generate traffic to remote sink.
  Ptr<SocketFactory> socketFactory = 
    c0.Get (0)->GetObject<Tcp> ();
  Ptr<Socket> localSocket = socketFactory->CreateSocket ();
  localSocket->Bind ();
  Simulator::ScheduleNow (&StartFlow, localSocket, nBytes,
                          ipInterfs.GetAddress (1), servPort);

  Config::ConnectWithoutContext ("/NodeList/*/ApplicationList/*/Rx", 
                   MakeCallback (&ApplicationTraceSink));

  std::ofstream ascii;
  ascii.open ("tcp-large-transfer.tr");
  PointToPointHelper::EnableAsciiAll (ascii);

  InternetStackHelper::EnablePcapAll ("tcp-large-transfer");

  Simulator::StopAt (Seconds(10));
  Simulator::Run ();
  Simulator::Destroy ();
}

void CloseConnection (Ptr<Socket> localSocket)
{
  localSocket->Close ();
}

void StartFlow(Ptr<Socket> localSocket, uint32_t nBytes, 
               Ipv4Address servAddress,
               uint16_t servPort)
{
 // NS_LOG_LOGIC("Starting flow at time " <<  Simulator::Now ().GetSeconds ());
  localSocket->Connect (InetSocketAddress (servAddress, servPort));//connect
  localSocket->SetConnectCallback (MakeCallback (&CloseConnection),
                                   Callback<void, Ptr<Socket> > (),
                                       Callback<void, Ptr<Socket> > ());
  //we want to close as soon as the connection is established
  //the tcp state machine and outgoing buffer will assure that
  //all of the data is delivered
  localSocket->SetSendCallback (MakeCallback (&WriteUntilBufferFull));
  WriteUntilBufferFull (localSocket, nBytes);
}

void WriteUntilBufferFull (Ptr<Socket> localSocket, uint32_t nBytes)
{
  // Perform series of 1040 byte writes (this is a multiple of 26 since
  // we want to detect data splicing in the output stream)
  std::cout << "nBytes = " <<nBytes <<"@"<<Simulator::Now().GetSeconds()<<std::endl;
  uint32_t writeSize = 1040;
  uint8_t data[writeSize];
  while (nBytes > 0) {
    uint32_t curSize= nBytes > writeSize ? writeSize : nBytes;
    for(uint32_t i = 0; i < curSize; ++i)
    {
      char m = toascii (97 + i % 26);
      data[i] = m;
    }
    localSocket->Send (data, curSize);
    nBytes -= curSize;
  }
}
