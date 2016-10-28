# 快速端口转发工具

## 简介

本项目是基于KCP开发的快速端口转发工具。

在介绍本项目之前先介绍几个概念。在C/S架构的程序体系中，有若干客户端(C)和少数服务端(S)。
客户端一般运行在用户电脑上，而服务端则部署到服务器提供商或云上。
不同的C/S软件提供不同的服务，内部的工作原理和机制也大不相同，
但在进行网络数据交换时，所有的C/S软件共享类似的工作模式。

服务端在启动时会绑定众所周知的IP和端口，客户端绑定临时IP和端口与服务端建立连接，
之后，C/S通过常用网络通信协议进行数据交换。

本项目工作在任何基于TCP协议的C/S软件之间，充当它们之间的 "代理人"。
我们使用tun-cli表示本项目客户端，使用tun-svr表示本项目服务端。
使用本项目的C/S软件不再直接进行网络通信。
我们以 [shadowsocks](https://github.com/shadowsocks/shadowsocks-libev)
为例简要地说明使用本项目后的C/S软件的通信过程。shadowsocks 的客户端为 ss-local，
服务端为 ss-server 。

首先 tun-cli 绑定固定的IP和端口，ss-local 之前指向 ss-server 的服务端地址改而指向 tun-cli 所绑定的地址。
此后，ss-local 的通信对象实际是 tun-cli ，但 tun-cli 是不能直接为 ss-local 提供服务的。
tun-cli 与 tun-svr 建立通信管道，tun-cli 将从 ss-local 收集的数据直接发往 tun-svr。
tun-svr 将"冒充" ss-local 与 ss-server 进行分组交换，而 tun-svr 从 ss-server 获取的数据将直接发送给 tun-cli。
tun-cli 将从 tun-svr 收到的数据转发给 ss-local。这样，ss-local 与 ss-server 就完成了一次通信。

上面描述的各程序具有如下关系图：

![](http://ruleless.github.io/images/github/fast-tun.png)
