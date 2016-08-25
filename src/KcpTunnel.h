#ifndef __KCPTUNNEL_H__
#define __KCPTUNNEL_H__

#include "FasttunBase.h"
#include "EventPoller.h"
#include "Cache.h"
#include "../kcp/ikcp.h"

NAMESPACE_BEG(tun)

class KcpTunnelGroup;

//--------------------------------------------------------------------------
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
class KcpTunnel
{
  public:
	struct Handler 
	{
		virtual void onRecv(KcpTunnel *pTunnel, const void *data, size_t datalen) = 0;
	};

    KcpTunnel(KcpTunnelGroup *pGroup)
			:mGroup(pGroup)
			,mKcpCb(NULL)
			,mHandler(NULL)
			,mConv(0)
			,mbAssignedRemoteAddr(false)
	{
		memset(&mRemoteAddr, 0, sizeof(mRemoteAddr));
		mCache = new MyCache(this);
	}
	
    virtual ~KcpTunnel();

	bool create(uint32 conv, const KcpArg &arg);
	void shutdown();

	int send(const void *data, size_t datalen);	

	bool input(const void *data, size_t datalen);

	void update(uint32 current);

	int _output(const void *data, size_t datalen);
	
	void _flushAll();
	void flush(const void *data, size_t datalen);

	inline uint32 getConv() const
	{
		return mConv;
	}			

	inline void setEventHandler(Handler *h)
	{
		mHandler = h;
	}

	inline KcpTunnelGroup* getGroup() const
	{
		return mGroup;
	}

	inline void setRemoteAddr(const SA *sa, socklen_t salen)
	{
		memcpy(&mRemoteAddr, sa, salen);
		mbAssignedRemoteAddr = true;
	}

	inline bool assignedRemoteAddr() const
	{
		return mbAssignedRemoteAddr;
	}		   
	
  private:
	typedef Cache<KcpTunnel> MyCache;
	
	KcpTunnelGroup *mGroup;
	ikcpcb *mKcpCb;
	Handler *mHandler;
	uint32 mConv;
	
	bool mbAssignedRemoteAddr;	
	sockaddr_in mRemoteAddr;

	MyCache *mCache;
};
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
class KcpTunnelGroup : public InputNotificationHandler
{
  public:
    KcpTunnelGroup(EventPoller *poller)
			:mEventPoller(poller)
			,mTunnels()
			,mFd(-1)
			,mbAssignedRemoteAddr(false)
			,mKcpArg(kcpmode::Fast3)
	{
		memset(&mLocalAddr, 0, sizeof(mLocalAddr));
		memset(&mRemoteAddr, 0, sizeof(mRemoteAddr));		
	}
	
    virtual ~KcpTunnelGroup();

	bool create(const char *localaddr, const char *remoteaddr = NULL);
	void shutdown();

	int _output(const void *data, size_t datalen);

	KcpTunnel* createTunnel(uint32 conv);
	void destroyTunnel(KcpTunnel *pTunnel);
	
	void update();

	// InputNotificationHandler
	virtual int handleInputNotification(int fd);

	inline void setKcpMode(const KcpArg &mode)
	{
		mKcpArg = mode;
	}

	inline bool assignedRemoteAddr() const
	{
		return mbAssignedRemoteAddr;
	}

	inline int getSockFd() const
	{
		return mFd;
	}
	
  private:
	typedef std::map<uint32, KcpTunnel *> Tunnels;	

	EventPoller *mEventPoller;

	Tunnels mTunnels;
	int mFd;
	
	bool mbAssignedRemoteAddr;
	sockaddr_in mLocalAddr;
	sockaddr_in mRemoteAddr;

	KcpArg mKcpArg;	
};
//--------------------------------------------------------------------------

NAMESPACE_END // namespace tun

#endif // __KCPTUNNEL_H__
