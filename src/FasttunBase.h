#ifndef __FASTTUNBASE_H__
#define __FASTTUNBASE_H__

#include "core/CillCore.h"
#include <list>

using core::mchar;
using core::wchar;

using core::tchar;
using core::tstring;

using core::uchar;
using core::ushort;
using core::uint;
using core::ulong;

using core::int64;
using core::int32;
using core::int16;
using core::int8;
using core::uint64;
using core::uint32;
using core::uint16;
using core::uint8;
using core::intptr;
using core::uintptr;

using core::MemoryStream;

using core::getTimeStamp;
using core::coreStrError;
using core::Ini;
using core::TimerHandler;
using core::TimerHandle;

typedef struct sockaddr SA;

#define GLOBAL_TUN_ID  100
#define LISTENQ 32
#define DEFAULT_CONF_PATH "/etc/fasttun/config.ini"

NAMESPACE_BEG(tun)

//--------------------------------------------------------------------------
enum EReason
{
	Reason_Success = 0,
	
	Reason_TimerExpired = -1,
	Reason_NoSuchPort = -2,
	Reason_GeneralNetwork = -3,
	Reason_CorruptedPacket = -4,
	Reason_NonExistentEntry = -5,
	Reason_WindowOverflow = -6,
	Reason_Inactivity = -7,
	Reason_ResourceUnavailable = -8,
	Reason_ClientDisconnected = -9,
	Reason_TransmitQueueFull = -10,
	Reason_ShuttingDown = -11,
	Reason_WebSocketError = -12,
};
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
struct Packet
{
    char *buf;
	size_t buflen;

	Packet() : buf(NULL), buflen(0)
	{		
	}

	virtual ~Packet()
	{
		if (buf)
			free(buf);
	}
};

struct TcpPacket : public Packet
{
    size_t sentlen;

	TcpPacket() : Packet(), sentlen(0)
	{
	}

	virtual ~TcpPacket()
	{
	}			
};
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
// ID Generator
template <class T, int MaxNum>
class IDGenerator
{
  public:
	bool genNewId(T& r)
	{
		if (mAvailableIds.empty())
		{
			return false;
		}

		r = mAvailableIds.front();
		mAvailableIds.pop_front();
		return true;
	}

	void restorId(const T &r)
	{
		mAvailableIds.push_back(r);
	}

  protected:
	IDGenerator() {}	
    virtual ~IDGenerator() {}
	
  protected:
	typedef std::list<T> IDList;

	IDList mAvailableIds;
};
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
struct HeartBeatRecord
{
    uint32 packetSentTime, packetRecvTime;

	static const uint32 HEARTBEAT_INTERVAL = 30000;
	static const uint32 CONNTIMEOUT_TIME = HEARTBEAT_INTERVAL*4;
	
	HeartBeatRecord()
			:packetSentTime(0)
			,packetRecvTime(0)
	{}

	bool isTimeout() const
	{
		uint32 curClock = core::getClock();
		if (curClock >= packetSentTime &&
			curClock >= packetRecvTime &&
			curClock-packetSentTime <= HEARTBEAT_INTERVAL*2 &&			 
			packetRecvTime > 0 && curClock-packetRecvTime >= CONNTIMEOUT_TIME)
		{
			return true;
		}
		return false;
	}
};
//--------------------------------------------------------------------------

extern core::Timers gTimer;

void daemonize(const char *path);
void print_stack_frames();

NAMESPACE_END // namespace tun

#endif // __FASTTUNBASE_H__
