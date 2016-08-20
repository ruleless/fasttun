#ifndef __CONNECTIONWRAPPER_H__
#define __CONNECTIONWRAPPER_H__

#include "FasttunBase.h"
#include "EventPoller.h"
#include "KcpTunnel.h"
#include "Connection.h"

NAMESPACE_BEG(tun)

class ConnectionWrapper
{
  public:
    ConnectionWrapper(EventPoller *poller, KcpTunnelGroup *pGroup)
			:mEventPoller(poller)
			,mpTunnelGroup(pGroup)
	{}
	
    virtual ~ConnectionWrapper();
  private:
	EventPoller *mEventPoller;
	KcpTunnelGroup *mpTunnelGroup;
};

NAMESPACE_END // namespace tun

#endif // __CONNECTIONWRAPPER_H__
