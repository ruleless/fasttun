#ifndef __UDPPACKETSENDER_H__
#define __UDPPACKETSENDER_H__

#include "fasttun_base.h"
#include "event_poller.h"

NAMESPACE_BEG(tun)

struct IUdpSender
{
    virtual int processSend(const void *data, size_t datalen) = 0;
    virtual void regOutputNotification(OutputNotificationHandler *p) = 0;
    virtual void unregOutputNotification(OutputNotificationHandler *p) = 0;
};

class UdpPacketSender : public OutputNotificationHandler
{
  public:
    UdpPacketSender(IUdpSender *pSender)
            :mpSender(pSender)
            ,mbRegForWrite(false)
            ,mPacketList()
    {}
    
    virtual ~UdpPacketSender();

    void send(const void *data, size_t datalen);

    // OutputNotificationHandler
    virtual int handleOutputNotification(int fd);

  private:
    void tryRegWriteEvent();
    void tryUnregWriteEvent();

    bool tryFlushRemainPacket();
    void cachePacket(const void *data, size_t datalen);
    
  private:
    typedef std::list<Packet *> PacketList;

    IUdpSender *mpSender;   
    
    bool mbRegForWrite;
    PacketList mPacketList;
};

NAMESPACE_END // namespace tun

#endif // __UDPPACKETSENDER_H__
