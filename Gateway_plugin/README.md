# Gateway Plugin

Gateway Plugin provides diagnostic and commissioning capabilities for Wi-SUN networks.

## Components

### 1. PHY Software

Contains the source code, configuration, and JSON schema for:

- DCU Diagnostics
- Gateway Commissioning

### 2. Services

Daemon service files for deployment in Debian 12 system:

- PHYSOFTWARE_DCU_GATEWAY.service

### 3. Source

Main source code implementing:

- Gateway commissioning logic
- DCU diagnostic functionality

## Build Instructions

### Compiling the Source

```bash
# Generate binary
$ make
```

### Deployment

```bash
# Install in Debian 12 system
$ make install
```

### Removal

```bash
# Remove from Debian 12 system
$ make uninstall
```

## Directory Structure

```
.
├── physoftware/
│   ├── dcu_diagnostics/
│   │   ├── src/
│   │   └── data.schema.json
│   └── Gateway-commissioning/
├── services/
│   └── PHYSOFTWARE_DCU_GATEWAY.service
└── src/
    └── main.c
```

## Configuration

### DCU Diagnostics

- Configuration file: `/usr/local/bin/PHY/dcu_diagnostics.conf`
- JSON Schema: `physoftware/dcu_diagnostics/data.schema.json`

### Gateway Commissioning

- Settings in Gateway-commissioning directory
- Service configuration in services directory

## Service Management

```bash
# Start the service
$ sudo systemctl start PHYSOFTWARE_DCU_GATEWAY

# Check status
$ sudo systemctl status PHYSOFTWARE_DCU_GATEWAY

# Enable on boot
$ sudo systemctl enable PHYSOFTWARE_DCU_GATEWAY
```

## Dependencies

- Debian 12
- Build essentials
- MQTT broker
- JSON parser libraries

## Troubleshooting

1. Service fails to start:

   - Check configuration files
   - Verify MQTT broker status
   - Review system logs

2. Build failures:
   - Ensure all dependencies are installed
   - Check compiler version
   - Verify source code integrity
