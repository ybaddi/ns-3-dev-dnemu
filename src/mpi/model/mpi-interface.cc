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
 * Author: George Riley <riley@ece.gatech.edu>
 */

// This object contains static methods that provide an easy interface
// to the necessary MPI information.

#include <iostream>
#include <iomanip>
#include <list>
#include <sys/types.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "mpi-interface.h"
#include "mpi-receiver.h"

#include "ns3/node.h"
#include "ns3/node-list.h"
#include "ns3/net-device.h"
#include "ns3/simulator.h"
#include "ns3/simulator-impl.h"
#include "ns3/nstime.h"
#include "ns3/realtime-simulator-impl.h"
#include "ns3/log.h"
#include "ns3/system-thread.h"

#ifdef NS3_MPI
#include <mpi.h>
#endif

NS_LOG_COMPONENT_DEFINE ("MpiInterface");

namespace ns3 {

SentBuffer::SentBuffer ()
{
  m_buffer = 0;
  m_request = 0;
}

SentBuffer::~SentBuffer ()
{
  delete [] m_buffer;
}

uint8_t*
SentBuffer::GetBuffer ()
{
  return m_buffer;
}

void
SentBuffer::SetBuffer (uint8_t* buffer)
{
  m_buffer = buffer;
}

#ifdef NS3_MPI
MPI_Request*
SentBuffer::GetRequest ()
{
  return &m_request;
}
#endif

uint32_t              MpiInterface::m_sid = 0;
uint32_t              MpiInterface::m_size = 1;
bool                  MpiInterface::m_initialized = false;
bool                  MpiInterface::m_enabled = false;
uint32_t              MpiInterface::m_rxCount = 0;
uint32_t              MpiInterface::m_txCount = 0;
std::list<SentBuffer> MpiInterface::m_pendingTx;
int                   MpiInterface::m_distsock = 0;
Ptr<SystemThread>     MpiInterface::m_readThread = 0;

#ifdef NS3_MPI
MPI_Request* MpiInterface::m_requests;
char**       MpiInterface::m_pRxBuffers;
#endif

void
MpiInterface::Destroy ()
{
#ifdef NS3_MPI
  for (uint32_t i = 0; i < GetSize (); ++i)
    {
      delete [] m_pRxBuffers[i];
    }
  delete [] m_pRxBuffers;
  delete [] m_requests;

  m_pendingTx.clear ();
#endif
}

uint32_t
MpiInterface::GetRxCount ()
{
  return m_rxCount;
}

uint32_t
MpiInterface::GetTxCount ()
{
  return m_txCount;
}

uint32_t
MpiInterface::GetSystemId ()
{
  if (!m_initialized)
    {
      Simulator::GetImplementation ();
      m_initialized = true;
    }
  return m_sid;
}

uint32_t
MpiInterface::GetSize ()
{
  if (!m_initialized)
    {
      Simulator::GetImplementation ();
      m_initialized = true;
    }
  return m_size;
}

bool
MpiInterface::IsEnabled ()
{
  if (!m_initialized)
    {
      Simulator::GetImplementation ();
      m_initialized = true;
    }
  return m_enabled;
}


// Port from ADIOI_cb_gather_name_array (cb_config_list.c)
struct proc_name_array {
  char **names;
  struct sockaddr_in **sockaddr;
};

static struct proc_name_array *procname_array = NULL;
//#define DEBUG 1
int
MpiInterface::gather_host_array()
{
  MPI_Comm comm = MPI_COMM_WORLD;
  char my_procname[MPI_MAX_PROCESSOR_NAME], **procname = 0;
  int *procname_len = NULL, my_procname_len, *disp = NULL, i;
  int commsize, commrank;
  int alloc_size;
  int ret, socksize = sizeof (struct sockaddr_in);
  struct ifaddrs *ifa_list;
  struct ifaddrs *ifa;
  struct sockaddr_in my_sockaddr, **sockaddr = 0;

  MPI_Comm_size(comm, &commsize);
  MPI_Comm_rank(comm, &commrank);
  MPI_Get_processor_name(my_procname, &my_procname_len);

  memset (&my_sockaddr, 0, sizeof (struct sockaddr_in));
  ret = getsockname (m_distsock, (struct sockaddr *)&my_sockaddr, (socklen_t *)&socksize);
  if (ret != 0)
    {
      std::cout << "getsockname err " << errno << " " << strerror (errno) << std::endl;
    }
  if (my_sockaddr.sin_port == 0)
    {
      std::cout << "getsockname ret port 0 " << std::endl;
      // XXX
      ret = getsockname (m_distsock, (struct sockaddr *)&my_sockaddr, (socklen_t *)&socksize);
    }
#ifdef DEBUG
   std::cout << "getsockname ret (last) port " << ntohs(my_sockaddr.sin_port) << std::endl;
#endif

  ret = getifaddrs (&ifa_list);
  if (ret != 0)
    {
      return -1;
    }

  for (ifa = ifa_list; ifa != NULL; ifa=ifa->ifa_next)
    {
      if (strncmp (ifa->ifa_name, "lo", 2) == 0)
        continue;
      if (ifa->ifa_addr->sa_family != AF_INET) 
        continue;

      memcpy (&my_sockaddr.sin_addr, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, 
              sizeof (struct in_addr));
    }


  /* allocate space for everything */
  procname_array = (struct proc_name_array *) malloc(sizeof(*procname_array));
  if (procname_array == NULL) {
    return -1;
  }

  procname_array->names = (char **) malloc(sizeof(char *) * commsize);
  if (procname_array->names == NULL) {
    return -1;
  }
  procname = procname_array->names; /* simpler to read */

  procname_len = (int *) malloc(commsize * sizeof(int));
  if (procname_len == NULL) { 
    return -1;
  }

  procname_array->sockaddr = (struct sockaddr_in **) malloc(sizeof(struct sockaddr_in *) * commsize);
  if (procname_array->sockaddr == NULL) {
    return -1;
  }

  /* gather lengths first */
  for (i=0; i < commsize; i++) {
    ret = MPI_Gather(&my_procname_len, 1, MPI_INT, 
                     procname_len, 1, MPI_INT, i, comm);
  }

  alloc_size = 0;
  for (i=0; i < commsize; i++) {
    /* add one to the lengths because we need to count the
     * terminator, and we are going to use this list of lengths
     * again in the gatherv.  
     */
#ifdef DEBUG
    fprintf(stderr, "rank%d: len[%d] = %d\n", commrank, i, procname_len[i]);
#endif
    alloc_size += ++procname_len[i];
  }

  procname[0] = (char *)malloc(alloc_size);
  if (procname[0] == NULL) {
    return -1;
  }

  for (i=1; i < commsize; i++) {
    procname[i] = procname[i-1] + procname_len[i-1];
  }

  disp = (int *)malloc(commsize * sizeof(int));
  disp[0] = 0;
  for (i=1; i < commsize; i++) {
    disp[i] = (int) (procname[i] - procname[0]);
  }


  /* now gather procname */
  for (i=0; i < commsize; i++) {
    MPI_Gatherv(my_procname, my_procname_len + 1, MPI_CHAR, 
                procname[0], procname_len, disp, MPI_CHAR,
                i, comm);
  }


  sockaddr = procname_array->sockaddr; /* simpler to read */
  sockaddr[0] = (struct sockaddr_in *)malloc(sizeof (struct sockaddr_in) * commsize);
  procname_len[0] = sizeof (struct sockaddr_in);
  if (sockaddr[0] == NULL) {
    return -1;
  }
  for (i=1; i < commsize; i++) {
    sockaddr[i] = sockaddr[i-1] + 1;
    procname_len[i] = sizeof (struct sockaddr_in);
  }
  disp[0] = 0;
  for (i=1; i < commsize; i++) {
    disp[i] = (int) ((char *)sockaddr[i] - (char *)sockaddr[0]);
  }

  /* now gather sockaddr */
  for (i=0; i < commsize; i++) {
    MPI_Gatherv(&my_sockaddr, sizeof (struct sockaddr_in), MPI_CHAR, 
                sockaddr[0], procname_len, disp, MPI_CHAR,
                i, comm);
  }

  /* no longer need the displacements or lengths */
  free(disp);
  free(procname_len);
  freeifaddrs (ifa_list);

#ifdef DEBUG
  for (i=0; i < commsize; i++) {
    fprintf(stderr, "rank%d: name[%d] = %s\n", commrank, i, procname[i]);
    fprintf(stderr, "rank%d: sockaddr[%d] = %s:%d\n", commrank, i, 
            inet_ntoa (sockaddr[i]->sin_addr), ntohs (sockaddr[i]->sin_port));
  }
#endif

  return 0;
}

bool
MpiInterface::IsInSameNode (int rank)
{
  int commrank;
  MPI_Comm_rank(MPI_COMM_WORLD, &commrank);

  if (strcmp (procname_array->names[rank], procname_array->names[commrank]) == 0)
    {
      return true;
    }
  return false;
}

struct sockaddr_in *
MpiInterface::GetSockAddress (int rank)
{
  return procname_array->sockaddr[rank];
}


void
MpiInterface::init_distributed_emu ()
{
  struct sockaddr_in addr;
  int ret;

  // Already created
  if (m_distsock != 0)
    {
      return;
    }

  m_distsock = socket (AF_INET, SOCK_DGRAM, 0);
  if (m_distsock < 0)
    {
      std::cout << "socket error " << errno << std::endl;
      return;
    }

  addr.sin_family = AF_UNSPEC;
  addr.sin_port = htons (0);
  //  addr.sin_port = htons (3939 + MpiInterface::GetSystemId ());
  addr.sin_addr.s_addr = INADDR_ANY;
  ret = bind (m_distsock, (struct sockaddr *)&addr, sizeof(addr));
  if (ret < 0)
    {
      std::cout << "bind error " << errno << std::endl;
      close (m_distsock);
      return;
    }


  gather_host_array ();

  m_readThread = Create<SystemThread> (MakeCallback (&MpiInterface::ReceiveSocketMessages));
  m_readThread->Start ();
}

int
MpiInterface::GetDistSocket ()
{
  return m_distsock;
}

void
MpiInterface::Enable (int* pargc, char*** pargv)
{
#ifdef NS3_MPI
  // Initialize the MPI interface
  MPI_Init (pargc, pargv);
  MPI_Barrier (MPI_COMM_WORLD);
  MPI_Comm_rank (MPI_COMM_WORLD, reinterpret_cast <int *> (&m_sid));
  MPI_Comm_size (MPI_COMM_WORLD, reinterpret_cast <int *> (&m_size));
  m_enabled = true;
  m_initialized = true;
  // Init Distributed Emulation
  init_distributed_emu ();

  // Post a non-blocking receive for all peers
  m_pRxBuffers = new char*[m_size];
  m_requests = new MPI_Request[m_size];
  for (uint32_t i = 0; i < GetSize (); ++i)
    {
      m_pRxBuffers[i] = new char[MAX_MPI_MSG_SIZE];
      MPI_Irecv (m_pRxBuffers[i], MAX_MPI_MSG_SIZE, MPI_CHAR, MPI_ANY_SOURCE, 0,
                 MPI_COMM_WORLD, &m_requests[i]);
    }

#else
  NS_FATAL_ERROR ("Can't use distributed simulator without MPI compiled in");
#endif
}

void
MpiInterface::SendPacket (Ptr<Packet> p, const Time& rxTime, uint32_t node, uint32_t dev)
{
#ifdef NS3_MPI
  SentBuffer sendBuf;
  m_pendingTx.push_back (sendBuf);
  std::list<SentBuffer>::reverse_iterator i = m_pendingTx.rbegin (); // Points to the last element

  uint32_t serializedSize = p->GetSerializedSize ();
  uint8_t* buffer =  new uint8_t[serializedSize + 16];
  i->SetBuffer (buffer);
  // Add the time, dest node and dest device
  uint64_t t = rxTime.GetNanoSeconds ();
  uint64_t* pTime = reinterpret_cast <uint64_t *> (buffer);
  *pTime++ = t;
  uint32_t* pData = reinterpret_cast<uint32_t *> (pTime);
  *pData++ = node;
  *pData++ = dev;
  // Serialize the packet
  p->Serialize (reinterpret_cast<uint8_t *> (pData), serializedSize);

  // Find the system id for the destination node
  Ptr<Node> destNode = NodeList::GetNode (node);
  uint32_t nodeSysId = destNode->GetSystemId ();

  MPI_Isend (reinterpret_cast<void *> (i->GetBuffer ()), serializedSize + 16, MPI_CHAR, nodeSysId,
             0, MPI_COMM_WORLD, (i->GetRequest ()));
  m_txCount++;
#else
  NS_FATAL_ERROR ("Can't use distributed simulator without MPI compiled in");
#endif
}

void
MpiInterface::ReceiveMessages ()
{ // Poll the non-block reads to see if data arrived
#ifdef NS3_MPI
  while (true)
    {
      int flag = 0;
      int index = 0;
      MPI_Status status;

      MPI_Testany (GetSize (), m_requests, &index, &flag, &status);
      if (!flag)
        {
          break;        // No more messages
        }
      int count;
      MPI_Get_count (&status, MPI_CHAR, &count);
      m_rxCount++; // Count this receive

      // Get the meta data first
      uint64_t* pTime = reinterpret_cast<uint64_t *> (m_pRxBuffers[index]);
      uint64_t nanoSeconds = *pTime++;
      uint32_t* pData = reinterpret_cast<uint32_t *> (pTime);
      uint32_t node = *pData++;
      uint32_t dev  = *pData++;

      Time rxTime = NanoSeconds (nanoSeconds);

      count -= sizeof (nanoSeconds) + sizeof (node) + sizeof (dev);

      Ptr<Packet> p = Create<Packet> (reinterpret_cast<uint8_t *> (pData), count, true);

      // Find the correct node/device to schedule receive event
      Ptr<Node> pNode = NodeList::GetNode (node);
      Ptr<MpiReceiver> pMpiRec = 0;
      uint32_t nDevices = pNode->GetNDevices ();
      for (uint32_t i = 0; i < nDevices; ++i)
        {
          Ptr<NetDevice> pThisDev = pNode->GetDevice (i);
          if (pThisDev->GetIfIndex () == dev)
            {
              pMpiRec = pThisDev->GetObject<MpiReceiver> ();
              break;
            }
        }

      NS_ASSERT (pNode && pMpiRec);

      // Schedule the rx event
      Simulator::ScheduleWithContext (pNode->GetId (), rxTime - Simulator::Now (),
                                      &MpiReceiver::Receive, pMpiRec, p);

      // Re-queue the next read
      MPI_Irecv (m_pRxBuffers[index], MAX_MPI_MSG_SIZE, MPI_CHAR, MPI_ANY_SOURCE, 0,
                 MPI_COMM_WORLD, &m_requests[index]);
    }
#else
  NS_FATAL_ERROR ("Can't use distributed simulator without MPI compiled in");
#endif
}

void
MpiInterface::TestSendComplete ()
{
#ifdef NS3_MPI
  std::list<SentBuffer>::iterator i = m_pendingTx.begin ();
  while (i != m_pendingTx.end ())
    {
      MPI_Status status;
      int flag = 0;
      MPI_Test (i->GetRequest (), &flag, &status);
      std::list<SentBuffer>::iterator current = i; // Save current for erasing
      i++;                                    // Advance to next
      if (flag)
        { // This message is complete
          m_pendingTx.erase (current);
        }
    }
#else
  NS_FATAL_ERROR ("Can't use distributed simulator without MPI compiled in");
#endif
}

void
MpiInterface::ReceiveSocketMessages ()
{ // Poll the non-block reads to see if data arrived
#ifdef NS3_MPI
  while (true)
    {
      int ret;
      char buffer [1500];
#if 0
      struct timeval timeout;
      fd_set fdset;

      memset (&timeout, 0, sizeof (struct timeval));
      FD_ZERO (&fdset);
      FD_SET (MpiInterface::GetDistSocket (), &fdset);
      ret = select (MpiInterface::GetDistSocket () + 1, &fdset, NULL, NULL, &timeout);
      if (ret < 0)
        {
          NS_ASSERT_MSG (0, "select error " << strerror (errno) << "(" << errno << ")");
          return;
        }
      else if (ret == 0)
        {
          //  NS_LOG_DEBUG ("select timedout");
          continue;
        }

      if (FD_ISSET (MpiInterface::GetDistSocket (), &fdset))
        {
#endif
          ret = recv (MpiInterface::GetDistSocket (), buffer, sizeof (buffer), 0);
          if (ret < 0)
            {
              NS_ASSERT_MSG (0, "recv error " << strerror (errno) << "(" << errno << ")");
              return;
            }
#if 0
        }
#endif

      m_rxCount++;
      NS_LOG_DEBUG ("recv succeed");

      uint32_t *pData = reinterpret_cast<uint32_t *> (buffer);
      uint32_t nodeId = *pData++;
      uint32_t devIdx  = *pData++;

      Ptr<Packet> p = Create<Packet> (reinterpret_cast<uint8_t *> (pData), 
                                      ret - (sizeof (uint32_t) * 2), true);

      Ptr<NetDevice> pThisDev = NodeList::GetNode (nodeId)->GetDevice (devIdx);
      NS_LOG_DEBUG ("nodeid is " << nodeId);
      NS_LOG_DEBUG ("netdev is " << devIdx);
      NS_LOG_DEBUG ("packet is " << p);


      // Find the correct node/device to schedule receive event
      Ptr<Node> pNode = NodeList::GetNode (nodeId);
      Ptr<MpiReceiver> pMpiRec = pThisDev->GetObject<MpiReceiver> ();
      NS_ASSERT (pNode && pMpiRec);

      // Schedule the rx event
  Ptr<RealtimeSimulatorImpl> impl = 
    DynamicCast<RealtimeSimulatorImpl> (Simulator::GetImplementation ());
  RealtimeSimulatorImpl *m_rtImpl = GetPointer (impl);
      m_rtImpl->ScheduleRealtimeNowWithContext (nodeId,
                                                MakeEvent (&MpiReceiver::Receive, pMpiRec, p));
    }
#else
  NS_FATAL_ERROR ("Can't use distributed emulation without MPI compiled in");
#endif
}

void 
MpiInterface::Disable ()
{
#ifdef NS3_MPI
  int flag = 0;
  MPI_Initialized (&flag);
  if (flag)
    {
      MPI_Finalize ();
      m_enabled = false;
      m_initialized = false;
    }
  else
    {
      NS_FATAL_ERROR ("Cannot disable MPI environment without Initializing it first");
    }

  NS_LOG_LOGIC ("Joining read thread");
  m_readThread->Join ();
  m_readThread = 0;
#else
  NS_FATAL_ERROR ("Can't use distributed simulator without MPI compiled in");
#endif
}


} // namespace ns3
