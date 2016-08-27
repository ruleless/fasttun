CC= gcc
CXX= g++
MAKE= make

CILLDIR:= ../cill
ROOT:= ..

CFLAGS+= -g -Wall
# CXXFLAGS+= -D_USE_KMEM -g -Wall -I $(CILLDIR) -I $(CILLDIR)/kmem
CXXFLAGS+= -g -Wall -I $(CILLDIR) -I $(CILLDIR)/kmem -I $(ROOT)

LDFLAGS+= -L $(CILLDIR)/lib -lcill -lkmem -L $(ROOT)/lib -lkcp -lpthread -lrt -lstdc++

RM= -rm -rf


COMMON_OBJS:= EventPoller.o SelectPoller.o EpollPoller.o Connection.o Listener.o \
	FastConnection.o

all:client.out server.out test.out

client.out:$(COMMON_OBJS) client.o
	$(CXX) -o $@ $^ $(LDFLAGS)
server.out:$(COMMON_OBJS) server.o
	$(CXX) -o $@ $^ $(LDFLAGS)
test.out:$(COMMON_OBJS) test.o
	$(CXX) -o $@ $^ $(LDFLAGS)

client.o: client.cpp EventPoller.h Listener.h Connection.h KcpTunnel.h FastConnection.h
server.o: server.cpp EventPoller.h Listener.h Connection.h KcpTunnel.h FastConnection.h Cache.h
test.o: test.cpp EventPoller.h Listener.h Connection.h KcpTunnel.h FastConnection.h Cache.h

EventPoller.o: EventPoller.cpp EventPoller.h SelectPoller.h EpollPoller.h
SelectPoller.o: SelectPoller.cpp SelectPoller.h EventPoller.h
EpollPoller.o: EpollPoller.cpp EpollPoller.h EventPoller.h
Listener.o: Listener.cpp Listener.h EventPoller.h
Connection.o: Connection.cpp Connection.h EventPoller.h
FastConnection.o: FastConnection.cpp FastConnection.h EventPoller.h KcpTunnel.h Connection.h Cache.h


clean:
	$(RM) *.o *.out

fake:
	@echo $(COMMON_OBJS)
