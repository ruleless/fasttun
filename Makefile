dirs:= cill
dirs+= kcp
dirs+= src

include build.mak

install-cli:
	$(MAKE) -C src install-cli
install-svr:
	$(MAKE) -C src install-svr

uninstall:
	$(MAKE) -C src uninstall

conf:
	echo "
	[local]
	listen=127.0.0.1:5085
	remote=127.0.0.1:29905

	[server]
	listen=127.0.0.1:29905
	connect=127.0.0.1:5080" > /etc/fast-tun.ini
