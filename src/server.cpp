#include "FasttunBase.h"
#include "Listener.h"
#include "Connection.h"
#include "KcpTunnel.h"
#include "GlobalTunnel.h"

using namespace tun;

class ServerTunnel;
static KcpTunnelGroup gTunnelManager;
static ServerTunnel* gGlobalTunnel = NULL;

//--------------------------------------------------------------------------
class ServerTunnel : public GlobalTunnel
{
  public:
    ServerTunnel(KcpTunnelGroup *pGroup)
			:GlobalTunnel(pGroup)
	{}
	
    virtual void onRecv(KcpTunnel *pTunnel, const void *data, size_t datalen)
	{
		const char *str = (const char *)data;
		printf("recv:%s\n", str);
	}
};
//--------------------------------------------------------------------------

int main(int argc, char *argv[])
{
	// initialise tracer
	core::createTrace();
	core::output2Console();
	core::output2Html("fasttun_server.html");

	// create event poller
	EventPoller *netPoller = EventPoller::create();	

	if (!gTunnelManager.initialise("0.0.0.0:29901", "127.0.0.1:29900"))
	{
		logErrorLn("initialise Tunnel Manager error!");
		delete netPoller;
		core::closeTrace();
		exit(1);
	}

	gGlobalTunnel = new ServerTunnel(&gTunnelManager);
	if (!gGlobalTunnel->initialise())
	{
		logErrorLn("initialise GloablTunnel error!");
		delete gGlobalTunnel;
		delete netPoller;
		core::closeTrace();
		exit(1);
	}

	for (;;)
	{
		netPoller->processPendingEvents(0.016);
		gTunnelManager.update();
	}

	gGlobalTunnel->finalise();
	delete gGlobalTunnel;
	gTunnelManager.finalise();

	delete netPoller;

	// close tracer
	core::closeTrace();
	exit(0);
}
