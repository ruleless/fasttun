CC= gcc
CXX= g++
MAKE= make

BINDIR:= /usr/local/bin
CILLDIR:= ../cill
ROOT:= ..

CFLAGS+= -g -Wall
CXXFLAGS+= -D_USE_NO_KMEM -rdynamic -g -Wall -I $(CILLDIR) -I $(CILLDIR)/kmem
# CXXFLAGS+= -g -Wall -I $(CILLDIR) -I $(CILLDIR)/kmem -I $(ROOT)

LDFLAGS+= -rdynamic -L $(CILLDIR)/lib -lcill -lkmem -L $(ROOT)/lib -lkcp -lpthread -lrt -lstdc++

RM= -rm -rf


COMMON_OBJS:= EventPoller.o SelectPoller.o EpollPoller.o Connection.o Listener.o \
	FastConnection.o FasttunBase.o UdpPacketSender.o DiskCache.o

.PHONY:all test clean install-cli install-svr fake
all:client.out server.out test.out
test:utest.out

client.out:$(COMMON_OBJS) client.o
	$(CXX) -o $@ $^ $(LDFLAGS)
server.out:$(COMMON_OBJS) server.o
	$(CXX) -o $@ $^ $(LDFLAGS)
test.out:$(COMMON_OBJS) test.o
	$(CXX) -o $@ $^ $(LDFLAGS)
utest.out:$(COMMON_OBJS) utest.o
	$(CXX) -o $@ $^ $(LDFLAGS) -lcppunit

client.o: client.cpp EventPoller.h Listener.h Connection.h KcpTunnel.h KcpTunnel.inl UdpPacketSender.h FastConnection.h
server.o: server.cpp EventPoller.h Listener.h Connection.h KcpTunnel.h KcpTunnel.inl UdpPacketSender.h FastConnection.h \
	Cache.h DiskCache.h
test.o: test.cpp EventPoller.h Listener.h Connection.h KcpTunnel.h KcpTunnel.inl UdpPacketSender.h FastConnection.h \
	Cache.h DiskCache.h
utest.o: utest.cpp EventPoller.h Listener.h Connection.h KcpTunnel.h KcpTunnel.inl UdpPacketSender.h FastConnection.h Cache.h \
	MessageReceiver.h DiskCache.h

FasttunBase.o: FasttunBase.cpp FasttunBase.h
EventPoller.o: EventPoller.cpp EventPoller.h SelectPoller.h EpollPoller.h FasttunBase.h
SelectPoller.o: SelectPoller.cpp SelectPoller.h EventPoller.h FasttunBase.h
EpollPoller.o: EpollPoller.cpp EpollPoller.h EventPoller.h FasttunBase.h
Listener.o: Listener.cpp Listener.h EventPoller.h FasttunBase.h
Connection.o: Connection.cpp Connection.h EventPoller.h FasttunBase.h
FastConnection.o: FastConnection.cpp FastConnection.h EventPoller.h KcpTunnel.h KcpTunnel.inl UdpPacketSender.h Connection.h \
	Cache.h DiskCache.h	FasttunBase.h MessageReceiver.h
UdpPacketSender.o: UdpPacketSender.cpp UdpPacketSender.h EventPoller.h FasttunBase.h
DiskCache.o: DiskCache.cpp DiskCache.h FasttunBase.h


install-cli:
	cp client.out $(BINDIR)/tun-cli
	cp ../script/tun-cli /etc/init.d/

install-svr:
	cp server.out $(BINDIR)/tun-svr
	cp ../script/tun-svr /etc/init.d/

uninstall:
	-$(RM) $(BINDIR)/tun-cli $(BINDIR)/tun-svr /etc/init.d/tun-cli /etc/init.d/tun-svr
	-service tun-cli stop
	-service -tun-svr stop
	-chkconfig --del tun-cli
	-chkconfig --del tun-svr


clean:
	$(RM) *.o *.out

fake:
	@echo $(COMMON_OBJS)
