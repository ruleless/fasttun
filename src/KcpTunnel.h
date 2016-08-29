#ifndef __KCPTUNNEL_H__
#define __KCPTUNNEL_H__

#include "FasttunBase.h"
#include "EventPoller.h"
#include "Cache.h"
#include "../kcp/ikcp.h"

NAMESPACE_BEG(tun)

//--------------------------------------------------------------------------
// Kcp参数
struct KcpArg
{
	int nodelay; // 是否启用 nodelay模式，0不启用；1启用
	int interval; // 协议内部工作的 interval，单位毫秒，比如 10ms或者 20ms
	int resend; // 快速重传模式，默认0关闭，可以设置2(2次ACK跨越将会直接重传)
	int nc; // 是否关闭流控，默认是0代表不关闭，1代表关闭
	int mtu;
};

NAMESPACE_BEG(kcpmode)
static KcpArg Normal = {0, 30, 2, 0, 1400,};
static KcpArg Fast   = {0, 20, 2, 1, 1400,};
static KcpArg Fast2  = {1, 20, 2, 1, 1400,};
static KcpArg Fast3  = {1, 10, 2, 1, 1400,};
NAMESPACE_END // namespace kcpmode
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
// Kcp管道组
struct ITunnel;
struct ITunnelGroup
{
	virtual ~ITunnelGroup() {};
    virtual ITunnel* createTunnel(uint32 conv) = 0;
	virtual void destroyTunnel(ITunnel *pTunnel) = 0;
};

template <bool IsServer>
class TunnelGroup : public ITunnelGroup
{
  public:
    TunnelGroup() : mFd(-1) {}
    virtual ~TunnelGroup() {}

	bool create(const char *addr)
	{
		if (!core::str2Ipv4(addr, mSockAddr))
		{
			logErrorLn("KcpTunnelGroup::create() invalid sockaddr! "<<addr);
			return false;
		}

		return this->_create();
	}
	bool create(const SA *sa, socklen_t salen)
	{
		memcpy(&mSockAddr, sa, salen);
		return this->_create();
	}
	bool _create()
	{
		// create socket
		mFd = socket(AF_INET, SOCK_DGRAM, 0);
		if (mFd < 0)
			return false;

		// set nonblocking
		if (!core::setNonblocking(mFd))
			return false;

		return true;
	}

	int _output(const void *data, size_t datalen)
	{
		return sendto(mFd, data, datalen, 0, (SA *)&mSockAddr, sizeof(mSockAddr));
	}	

  protected:
	sockaddr_in mSockAddr;
	int mFd;
};

template <>
class TunnelGroup<true> : public ITunnelGroup
{
  public:
    TunnelGroup<true>() : mFd(-1) {}
    virtual ~TunnelGroup<true>() {}

	bool create(const char *addr)
	{
		sockaddr_in sockaddr;
		if (!core::str2Ipv4(addr, sockaddr))
		{
			logErrorLn("KcpTunnelGroup::create() invalid sockaddr! "<<addr);
			return false;
		}
		return this->create((SA *)&sockaddr, sizeof(sockaddr));
	}
	bool create(const SA *sa, socklen_t salen)
	{
		// create socket
		mFd = socket(AF_INET, SOCK_DGRAM, 0);
		if (mFd < 0)
			return false;

		// set nonblocking
		if (!core::setNonblocking(mFd))
			return false;		

		// bind local address
		if (bind(mFd, sa, salen) < 0)
		{
			logErrorLn("KcpTunnelGroup::create() bind local address err! "<<coreStrError());
			return false;
		}

		return true;
	}
	
	int _getSockFd() const
	{
		return mFd;
	}

  protected:
	int mFd;
};
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
// Kcp管道
struct KcpTunnelHandler
{
	virtual void onRecv(const void *data, size_t datalen) = 0;
};

struct ITunnel
{
	virtual ~ITunnel() {};

	virtual int send(const void *data, size_t datalen) = 0;
	virtual int _output(const void *data, size_t datalen) = 0;

	virtual uint32 getConv() const = 0;

	virtual void setEventHandler(KcpTunnelHandler *h) = 0;
	virtual ikcpcb* _debugGetKcpCb() const = 0;
};

template <bool IsServer>
class Tunnel : public ITunnel
{
	typedef TunnelGroup<IsServer> MyTunnelGroup;
  public:
	virtual int _output(const void *data, size_t datalen)
	{
		return this->mpGroup->_output(data, datalen);
	}

	void onRecvPeerAddr(const SA *sa, socklen_t salen) {}

  protected:
	Tunnel(MyTunnelGroup *pGroup)
			:mpGroup(pGroup)
	{
	}
    virtual ~Tunnel() {}
	
  protected:
	MyTunnelGroup *mpGroup;	   	
};

template <>
class Tunnel<true> : public ITunnel
{
	typedef TunnelGroup<true> MyTunnelGroup;
	typedef Cache< Tunnel<true> > MyCache;
  public:    
	virtual int _output(const void *data, size_t datalen)
	{
		if (this->mAddrSettled)
		{
			this->mCache->flushAll();
			return sendto(this->mpGroup->_getSockFd(),
						  data, datalen, 0,
						  (SA *)&this->mSockAddr, sizeof(this->mSockAddr));
		}
		else
		{
			this->mCache->cache(data, datalen);
			return datalen;
		}
	}	

	void onRecvPeerAddr(const SA *sa, socklen_t salen)
	{
		memcpy(&mSockAddr, sa, salen);
		mAddrSettled = true;
	}	

	void flush(const void *data, size_t datalen)
	{
		sendto(this->mpGroup->_getSockFd(), data, datalen,
			   0, (SA *)&this->mSockAddr, sizeof(this->mSockAddr));
	}

  protected:
	Tunnel(MyTunnelGroup *pGroup)
			:mpGroup(pGroup)
			,mAddrSettled(false)
	{
		this->mCache = new MyCache(this);
	}
    virtual ~Tunnel()
	{
		delete this->mCache;
	}
	
  protected:
	MyTunnelGroup *mpGroup;
	
	bool mAddrSettled;
	sockaddr_in mSockAddr;

	MyCache *mCache;
};

template <bool IsServer>
class KcpTunnel : public Tunnel<IsServer>
{
	typedef TunnelGroup<IsServer> MyTunnelGroup;
	typedef Cache< KcpTunnel<IsServer> > SndCache;
  public:	
    KcpTunnel(MyTunnelGroup *pGroup)
			:Tunnel<IsServer>(pGroup)
			,mKcpCb(NULL)
			,mHandler(NULL)
			,mConv(0)
			,mSentCount(0)
			,mRecvCount(0)
	{
		this->mSndCache = new SndCache(this);
	}
	
    virtual ~KcpTunnel();

	bool create(uint32 conv, const KcpArg &arg);
	void shutdown();

	virtual int send(const void *data, size_t datalen);
	virtual uint32 getConv() const
	{
		return mConv;
	}
	virtual void setEventHandler(KcpTunnelHandler *h)
	{
		mHandler = h;
	}
	virtual ikcpcb* _debugGetKcpCb() const
	{
		return mKcpCb;
	}

	bool input(const void *data, size_t datalen);
	uint32 update(uint32 current);

	void _flushAll();
	bool _canFlush() const;
	void flush(const void *data, size_t datalen);
	
  private:		
	ikcpcb *mKcpCb;
	KcpTunnelHandler *mHandler;
	uint32 mConv;

	int mSentCount;
	int mRecvCount;

	SndCache *mSndCache;
};
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
// Kcp管道组(最终实现)
template <bool IsServer>
class KcpTunnelGroup : public InputNotificationHandler, public TunnelGroup<IsServer>
{
	typedef TunnelGroup<IsServer> Supper;
	typedef KcpTunnel<IsServer> Tun;
  public:		
    KcpTunnelGroup(EventPoller *poller)
			:Supper()
			,mEventPoller(poller)
			,mTunnels()
			,mKcpArg(kcpmode::Fast3)
	{
	}
	
    virtual ~KcpTunnelGroup();

	bool create(const char *addr);
	bool create(const SA* sa, socklen_t salen);
	bool _create();
	void shutdown();

	int _output(const void *data, size_t datalen);

	virtual ITunnel* createTunnel(uint32 conv);
	virtual void destroyTunnel(ITunnel *pTunnel);

	uint32 update();

	// InputNotificationHandler
	virtual int handleInputNotification(int fd);

	inline void setKcpMode(const KcpArg &mode)
	{
		mKcpArg = mode;
	}
	
  private:
	typedef std::map<uint32, Tun *> Tunnels;

	EventPoller *mEventPoller;
	Tunnels mTunnels;
	KcpArg mKcpArg;	
};
//--------------------------------------------------------------------------

NAMESPACE_END // namespace tun

#include "KcpTunnel.inl"

#endif // __KCPTUNNEL_H__
