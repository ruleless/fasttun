#include "UdpPacketSender.h"

NAMESPACE_BEG(tun)

UdpPacketSender::~UdpPacketSender()
{
    tryUnregWriteEvent();
    PacketList::iterator it = mPacketList.begin();
    for (; it != mPacketList.end(); ++it)
        delete *it;
    mPacketList.clear();
}

void UdpPacketSender::send(const void *data, size_t datalen)
{
    if (tryFlushRemainPacket())
    {
        int sentlen = mpSender->processSend(data, datalen);
        // int sentlen = -1;
        if (sentlen == datalen)
            return;
    }

    cachePacket(data, datalen);
    tryRegWriteEvent();
}

int UdpPacketSender::handleOutputNotification(int fd)
{
    tryFlushRemainPacket();
}

void UdpPacketSender::tryRegWriteEvent()
{
    if (!mbRegForWrite)
    {
        mbRegForWrite = true;
        mpSender->regOutputNotification(this);
    }
}

void UdpPacketSender::tryUnregWriteEvent()      
{
    if (mbRegForWrite)
    {
        mbRegForWrite = false;
        mpSender->unregOutputNotification(this);
    }   
}

bool UdpPacketSender::tryFlushRemainPacket()
{
    if (mPacketList.empty())
    {
        return true;
    }
    
    PacketList::iterator it = mPacketList.begin();
    for (; it != mPacketList.end();)
    {
        Packet *p = *it;
        int sentlen = mpSender->processSend(p->buf, p->buflen);
        
        if (p->buflen == sentlen)
        {
            delete p;
            mPacketList.erase(it++);
            continue;
        }
        
        break;
    }

    if (mPacketList.empty())
    {
        tryUnregWriteEvent();
        return true;
    }
        
    return false;
}

void UdpPacketSender::cachePacket(const void *data, size_t datalen)
{
    Packet *p = new Packet(); assert(p && "udppacket != NULL");
    p->buf = (char *)malloc(datalen); assert(p->buf && "udppacket->buf != NULL");
    memcpy(p->buf, data, datalen);
    p->buflen = datalen;
    mPacketList.push_back(p);
}

NAMESPACE_END // namespace tun
