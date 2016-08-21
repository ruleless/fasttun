#ifndef __GLOBALTUNNEL_H__
#define __GLOBALTUNNEL_H__

#include "FasttunBase.h"
#include "KcpTunnel.h"

NAMESPACE_BEG(tun)

class GlobalTunnel : public KcpTunnel::Handler
{
  public:
    GlobalTunnel(KcpTunnelGroup *pGroup)
			:mTun(NULL)
			,mpGroupTun(pGroup)
	{
		assert(mpGroupTun && "mpGroupTun != NULL");
	}
	
    virtual ~GlobalTunnel();

	bool initialise();
	void finalise();

	int send(const void *data, size_t datalen);

	virtual void onRecv(KcpTunnel *pTunnel, const void *data, size_t datalen);
	
  private:
	KcpTunnel *mTun;
	KcpTunnelGroup *mpGroupTun;
};

NAMESPACE_END // namespace tun

#endif // __GLOBALTUNNEL_H__
