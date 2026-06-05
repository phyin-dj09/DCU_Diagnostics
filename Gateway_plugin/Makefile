
CC=gcc
CFLAGS=-Wall -O2 -Iphysoftware/dcu_diagnostics/include -Iphysoftware/Gateway-commissioning/include

BIN=PHYSOFTWARE_DCU_GATEWAY
LIBS=-ljansson -lpaho-mqtt3c -lpthread

SRC=src/main.c \
	physoftware/dcu_diagnostics/src/dcu_diagnostics_publisher.c \
	physoftware/Gateway-commissioning/src/cellular_network.c

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LIBS)



install:
	mkdir -p /usr/local/bin/PHY
	cp physoftware/dcu_diagnostics/src/dcu_diagnostics.conf /usr/local/bin/PHY/
	cp physoftware/Gateway-commissioning/src/dcu_network.conf /usr/local/bin/PHY/
	cp physoftware/dcu_diagnostics/src/data.schema.json /usr/local/bin/PHY/
	cp $(BIN) /usr/local/bin/PHY/$(BIN)
	cp services/PHYSOFTWARE_DCU_GATEWAY.service /etc/systemd/system/PHYSOFTWARE_DCU_GATEWAY.service
	systemctl daemon-reload
	systemctl enable PHYSOFTWARE_DCU_GATEWAY
	systemctl start PHYSOFTWARE_DCU_GATEWAY

stop:
	systemctl stop PHYSOFTWARE_DCU_GATEWAY || true

uninstall: stop
	rm -rf /usr/local/bin/PHY
	rm -f /etc/systemd/system/PHYSOFTWARE_DCU_GATEWAY.service
	systemctl daemon-reload

clean:
	rm -f $(BIN)

.PHONY: all clean install
