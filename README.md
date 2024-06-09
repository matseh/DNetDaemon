# DNetDaemon

This is a port of Matt Dillon's DNet 2.32 for Unix from Fred Fish disk 897 to macOS.

## Changes

### dnet

The dnet executable has new options.

- `-D <serialPort>` to start listening for incoming connections on the specified serial port.
  The serial port is specified as the path to the corresponding call-up device, e.g. `-D/dev/cu.PL2303G-USBtoUART310`. Logging will go to stderr when using this option.
- `-b <baudRate>` to set the baud rate of the serial port specified with the `-D` option.
