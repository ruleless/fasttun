#ifndef __KCPTUNNEL_H__
#define __KCPTUNNEL_H__

NAMESPACE_BEG(tun)

class KcpTunnul : public Tunnel
{
  public:
    KcpTunnul()
			:Tunnel()
	{}
	
    virtual ~KcpTunnul();

	
};

NAMESPACE_END // namespace tun

#endif // __KCPTUNNEL_H__
