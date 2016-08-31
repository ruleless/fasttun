#include "Listener.h"

NAMESPACE_BEG(tun)

Listener::~Listener()
{
	finalise();
}

bool Listener::create(const char *ip, int port)
{
	struct sockaddr_in remoteAddr;
	remoteAddr.sin_family = AF_INET;
	remoteAddr.sin_port = htons(port);
	if (inet_pton(AF_INET, ip, &remoteAddr.sin_addr) < 0)
	{
		logErrorLn("Listener::create()  illegal ip("<<ip<<")");
		return false;
	}

	return create((const SA *)&remoteAddr, sizeof(remoteAddr));
}

bool Listener::create(const SA *sa, socklen_t salen)
{	
	if (mFd >= 0)
	{
		logErrorLn("Listener already created!");
		return false;
	}

	mFd = socket(AF_INET, SOCK_STREAM, 0);
	if (mFd < 0)
	{
		logErrorLn("Listener::create()  create socket error! "<<coreStrError());
		return false;
	}

	// set nonblocking	
	if (!core::setNonblocking(mFd))
	{
		logErrorLn("Listener::create()  set nonblocking error! "<<coreStrError());
		goto err_1;
	}
	
	if (bind(mFd, sa, salen) < 0)
	{
		logErrorLn("Listener::create()  bind error! "<<coreStrError());
		goto err_1;
	}

	if (listen(mFd, LISTENQ) < 0)
	{
		logErrorLn("Listener::create()  listen failed! "<<coreStrError());
		goto err_1;
	}

	if (!mEventPoller->registerForRead(mFd, this))
	{
		logErrorLn("Listener::create()  registerForRead failed! "<<coreStrError());
		goto err_1;
	}

	return true;

err_1:
	close(mFd);
	mFd = -1;
	
	return false;
}

void Listener::finalise()
{
	if (mFd < 0)
		return;
	
	mEventPoller->deregisterForRead(mFd);
	close(mFd);
	mFd = -1;
}

int Listener::handleInputNotification(int fd)
{
	struct sockaddr_in addr;
	socklen_t addrlen;
	int newConns = 0;
	while (newConns++ < 32)
	{
		int connfd = accept(fd, (SA *)&addr, &addrlen);
		if (connfd < 0)
		{
			// logTraceLn("accept failed! "<<coreStrError());
			break;
		}
		else
		{
#if 0
			struct sockaddr_in localAddr, remoteAddr;
			socklen_t localAddrLen = sizeof(localAddr), remoteAddrLen = sizeof(remoteAddr);
			if (getsockname(connfd, (SA *)&localAddr, &localAddrLen) == 0 &&
				getpeername(connfd, (SA *)&remoteAddr, &remoteAddrLen) == 0)
			{
				char remoteip[MAX_BUF] = {0}, localip[MAX_BUF] = {0};
				if (inet_ntop(AF_INET, &localAddr.sin_addr, localip, sizeof(localip)) &&
					inet_ntop(AF_INET, &remoteAddr.sin_addr, remoteip, sizeof(remoteip)))
				{
					char acceptLog[1024] = {0};
					snprintf(acceptLog, sizeof(acceptLog), "(%s:%d) accept from %s:%d",
							 localip, ntohs(localAddr.sin_port),
							 remoteip, ntohs(remoteAddr.sin_port));
					logTraceLn(acceptLog);
				}
			}
#endif 
			
			if (mHandler)
			{
				mHandler->onAccept(connfd);
			}
		}
	}
	
	return 0;
}

NAMESPACE_END // namespace tun
