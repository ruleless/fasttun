CC= clang
CXX= clang++
MAKE= make

CFLAGS+= -g -Wall
CXXFLAGS+= -g -Wall

LDFLAGS+= -lrt

RM= -rm -rf


OBJS:= EpollPoller.o  EventPoller.o  SelectPoller.o

all:client.out

client.out:$(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

EpollPoller.o:
EventPoller.o:
SelectPoller.o:

fake:
	@echo $(OBJS)
