#include "Listener.h"

NAMESPACE_BEG(tun)

Listener::~Listener()
{
	finalise();
}

bool Listener::create(const char *ip, int port)
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
	int fstatus = fcntl(mFd, F_GETFL);
	if (fstatus < 0)
	{
		logErrorLn("Listener::create()  get file status error! "<<coreStrError());
		goto err_1;
	}
	if (fcntl(mFd, F_SETFL, fstatus|O_NONBLOCK) < 0)
	{
		logErrorLn("Listener::create()  set nonblocking error! "<<coreStrError());
		goto err_1;
	}

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, ip, &addr.sin_addr) < 0)
	{		
		logErrorLn("Listener::create()  illegal ip("<<ip<<")");
		goto err_1;
	}
	if (bind(mFd, (SA *)&addr, sizeof(addr)) < 0)
	{
		logErrorLn("Listener::create()  bind error! "<<coreStrError());
		goto err_1;
	}

	if (listen(mFd, 5) < 0)
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
			struct sockaddr_in addr;
			socklen_t addrlen;
			if (getpeername(connfd, (SA *)&addr, &addrlen) == 0)
			{
				char ip[MAX_BUF] = {0};				
				if (inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip)))
				{
					char acceptLog[1024] = {0};
					snprintf(acceptLog, sizeof(acceptLog), "accept from %s:%d", ip, ntohs(addr.sin_port));
					logTraceLn(acceptLog);
				}
			}
			if (mHandler)
			{
				mHandler->onAccept(connfd);
			}
		}
	}
	
	return 0;
}

NAMESPACE_END // namespace tun
