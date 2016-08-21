#ifndef __KCPTUNNEL_H__
#define __KCPTUNNEL_H__

#include "FasttunBase.h"
#include "../kcp/ikcp.h"

NAMESPACE_BEG(tun)

class KcpTunnelGroup;

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
	{}
	
    virtual ~KcpTunnel();

	bool create(uint32 conv);
	void shutdown();

	int send(const void *data, size_t datalen);

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

	bool input(const void *data, size_t datalen);

	void update(uint32 current);
  private:
	KcpTunnelGroup *mGroup;
	ikcpcb *mKcpCb;
	Handler *mHandler;
	uint32 mConv;
};
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
class KcpTunnelGroup
{
  public:
    KcpTunnelGroup()
			:mTunnels()
			,mFd(-1)
	{
		memset(&mLocalAddr, 0, sizeof(mLocalAddr));
		memset(&mRemoteAddr, 0, sizeof(mRemoteAddr));
	}
	
    virtual ~KcpTunnelGroup();

	bool initialise(const char *localaddr, const char *remoteaddr);
	void finalise();

	int _send(const void *data, size_t datalen);

	KcpTunnel* createTunnel(uint32 conv);
	void destroyTunnel(KcpTunnel *pTunnel);

	void update();
  private:
	typedef std::map<uint32, KcpTunnel *> Tunnels;

	Tunnels mTunnels;
	int mFd;
	sockaddr_in mLocalAddr;
	sockaddr_in mRemoteAddr;
};
//--------------------------------------------------------------------------

NAMESPACE_END // namespace tun

#endif // __KCPTUNNEL_H__
