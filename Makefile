dirs:= cill
dirs+= kcp
dirs+= src
CONF_CONTENT = ""

include build.mak

install-cli:
	$(MAKE) -C src install-cli
install-svr:
	$(MAKE) -C src install-svr

uninstall:
	$(MAKE) -C src uninstall

conf:
	echo "[local]" > /etc/fast-tun.ini
	echo "listen=127.0.0.1:5085" >> /etc/fast-tun.ini
	echo "remote=127.0.0.1:29905" >> /etc/fast-tun.ini
	echo "" >> /etc/fast-tun.ini
	echo "[server]" >> /etc/fast-tun.ini
	echo "listen=127.0.0.1:29905" >> /etc/fast-tun.ini
	echo "connect=127.0.0.1:5080" >> /etc/fast-tun.ini
