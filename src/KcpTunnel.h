#ifndef __KCPTUNNEL_H__
#define __KCPTUNNEL_H__

#include "FasttunBase.h"
#include "ikcp.h"

NAMESPACE_BEG(tun)

class KcpTunnul
{
  public:
	struct Handler 
	{
		virtual void onRecv(KcpTunnul *pTunnel, const void *data, size_t datelen) = 0;
	};

    KcpTunnul()
			:mKcpCb(NULL)
			,mHandler(NULL)
	{}
	
    virtual ~KcpTunnul();

	bool create();
	void shutdown();

	int send(const void *data, size_t datalen);

	inline void setEventHandler(Handler *h)
	{
		mHandler = h;
	}

	void update(uint32 current);
  private:
	ikcpcb *mKcpCb;
	Handler *mHandler;
};

NAMESPACE_END // namespace tun

#endif // __KCPTUNNEL_H__
