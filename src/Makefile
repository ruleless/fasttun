CC= clang
CXX= clang++
MAKE= make

CILLDIR:= ../cill
ROOT:= ..

CFLAGS+= -g -Wall
# CXXFLAGS+= -D_USE_KMEM -g -Wall -I $(CILLDIR) -I $(CILLDIR)/kmem
CXXFLAGS+= -g -Wall -I $(CILLDIR) -I $(CILLDIR)/kmem -I $(ROOT)

LDFLAGS+= -L $(CILLDIR)/lib -lcill -lkmem -L $(ROOT)/lib -lkcp -lpthread -lrt -lstdc++

RM= -rm -rf


COMMON_OBJS:= EventPoller.o SelectPoller.o EpollPoller.o Connection.o Listener.o \
	KcpTunnel.o GlobalTunnel.o

all:client.out server.out

client.out:$(COMMON_OBJS) client.o
	$(CXX) -o $@ $^ $(LDFLAGS)
server.out:$(COMMON_OBJS) server.o
	$(CXX) -o $@ $^ $(LDFLAGS)

client.o: client.cpp EventPoller.h Listener.h Connection.h KcpTunnel.h GlobalTunnel.h
server.o: server.cpp EventPoller.h Listener.h Connection.h KcpTunnel.h GlobalTunnel.h

EventPoller.o: EventPoller.cpp EventPoller.h SelectPoller.h EpollPoller.h
SelectPoller.o: SelectPoller.cpp SelectPoller.h EventPoller.h
EpollPoller.o: EpollPoller.cpp EpollPoller.h EventPoller.h
Listener.o: Listener.cpp Listener.h EventPoller.h
Connection.o: Connection.cpp Connection.h EventPoller.h
KcpTunnel.o: KcpTunnel.cpp KcpTunnel.h
GlobalTunnel.o: GlobalTunnel.cpp GlobalTunnel.h KcpTunnel.h


clean:
	$(RM) *.o *.out

fake:
	@echo $(COMMON_OBJS)
