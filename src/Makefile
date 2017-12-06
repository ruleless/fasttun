CC= gcc
CXX= g++
MAKE= make

BINDIR:= /usr/local/bin
CILLDIR:= ../cill
ROOT:= ..

CFLAGS+= -g -Wall -D_USE_KMEM
CXXFLAGS+= -rdynamic -g -Wall -D_USE_KMEM -I $(CILLDIR) -I $(CILLDIR)/kmem
# CXXFLAGS+= -g -Wall -I $(CILLDIR) -I $(CILLDIR)/kmem -I $(ROOT)

LDFLAGS+= -rdynamic -L $(CILLDIR)/lib -lcill -llog -lkmem -L $(ROOT)/lib -lkcp -lpthread -lrt -lstdc++

RM= -rm -rf


COMMON_OBJS:= event_poller.o select_poller.o epoll_poller.o connection.o listener.o \
	fast_connection.o fasttun_base.o udppacket_sender.o disk_cache.o

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

client.o: client.cpp event_poller.h listener.h connection.h kcp_tunnel.h kcp_tunnel.inl udppacket_sender.h fast_connection.h
server.o: server.cpp event_poller.h listener.h connection.h kcp_tunnel.h kcp_tunnel.inl udppacket_sender.h fast_connection.h \
	cache.h disk_cache.h
test.o: test.cpp event_poller.h listener.h connection.h kcp_tunnel.h kcp_tunnel.inl udppacket_sender.h fast_connection.h \
	cache.h disk_cache.h
utest.o: utest.cpp event_poller.h listener.h connection.h kcp_tunnel.h kcp_tunnel.inl udppacket_sender.h fast_connection.h cache.h \
	message_receiver.h disk_cache.h

fasttun_base.o: fasttun_base.cpp fasttun_base.h
event_poller.o: event_poller.cpp event_poller.h select_poller.h epoll_poller.h fasttun_base.h
select_poller.o: select_poller.cpp select_poller.h event_poller.h fasttun_base.h
epoll_poller.o: epoll_poller.cpp epoll_poller.h event_poller.h fasttun_base.h
listener.o: listener.cpp listener.h event_poller.h fasttun_base.h
connection.o: connection.cpp connection.h event_poller.h fasttun_base.h
fast_connection.o: fast_connection.cpp fast_connection.h event_poller.h kcp_tunnel.h kcp_tunnel.inl udppacket_sender.h connection.h \
	cache.h disk_cache.h fasttun_base.h message_receiver.h
udppacket_sender.o: udppacket_sender.cpp udppacket_sender.h event_poller.h fasttun_base.h
disk_cache.o: disk_cache.cpp disk_cache.h fasttun_base.h


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
