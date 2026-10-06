# USB support

The USB stack is shared by the live system, installed system and BIOS/UEFI
installer. Rebuild with `make all` and boot the rebuilt image; an already
installed kernel is not updated merely by rebuilding the ISO.

## Controllers and device classes

| Controller | Direct storage | Pointer input | External hubs |
| --- | --- | --- | --- |
| UHCI | Full-speed BOT/SCSI | Low/full-speed HID | Not implemented |
| OHCI | Full-speed BOT/SCSI | Low/full-speed HID | Not implemented |
| EHCI | High-speed BOT/SCSI | High-speed HID; direct low/full-speed devices use a UHCI/OHCI companion | Not implemented |
| xHCI | Full/high/SuperSpeed BOT/SCSI | Low/full/high/SuperSpeed HID | USB hub enumeration, port power/reset, nested routes and disconnect propagation |

The storage class supports LUN 0, 512-byte logical sectors, READ CAPACITY(10),
READ(10), WRITE(10), REQUEST SENSE and SYNCHRONIZE CACHE. USB disks appear in
`devices` independently of their filesystem. Mounting still requires Barnix's
ext2 profile. FAT/exFAT/NTFS and UAS-only devices are not supported.

HID report descriptors provide signed/packed axes, report IDs, up to five
buttons, wheel, horizontal pan and absolute coordinates. Boot mice fall back to
three-byte reports when report protocol cannot be used. Each interface of a
composite receiver is considered until a supported pointer/storage interface is
found. There is one active class interface per physical device; USB keyboards,
multiple simultaneous class interfaces on one composite device, vendor-specific
mouse configuration, touch gestures and other USB classes are not implemented.

## Hardware behavior

- PCI discovery covers all buses and multifunction devices. Firmware ownership
  is handed over before controller reset. EHCI routes direct full/low-speed
  connections to companion controllers.
- Reset, power settling and debounce use PIT channel 2, not CPU spin estimates.
  Controller/transfer waits are bounded. An idle mouse leaves an interrupt
  request in hardware instead of blocking keyboard input.
- DMA buffers are static, below 4 GiB and identity mapped. xHCI rings and buffers
  are aligned so individual transfer segments do not cross 64 KiB boundaries.
  EHCI includes zeroed high DMA address fields for 64-bit capable controllers.
- USB shares the PCI low-MMIO allocator under UEFI. Root-bus 64-bit BARs above
  4 GiB can be relocated into a reserved free low range. Bridge children still
  require usable firmware mappings. IOMMU remapping, suspend/resume and PCI
  controller hotplug are outside this implementation.
- xHCI storage has independent bulk rings; mouse interrupts are processed while
  waiting for disk transfers. Short transfers and BOT status/signature/tag errors
  cannot be returned as successful complete block reads.
- Disconnects release only the affected pointer's buttons. Disk enumeration
  generations prevent reusing an old filesystem mount after a reconnect.
  A failed command on xHCI stops that host before its DMA storage can be reused.

The inventory has 48 USB names: legacy root ports first, followed by up to 16
xHCI storage devices in free-entry order. Limits are eight UHCI hosts (16 ports),
eight combined EHCI/OHCI hosts (16 exposed ports), two xHCI hosts (16 device slots
each), and five routing levels of xHCI hubs. Hub ports above 15 are not enumerated.
These are bounded kernel allocations, not claims of unlimited USB support.

## Diagnose a physical machine

Run `devices` with the current build. It prints controller counts and raw disk
names. A detected but uninitialized controller is different from a listed disk
whose filesystem cannot be mounted. Connected legacy ports with unsupported or
failed enumeration are also reported. `devices lsblk` reports the recognized
filesystem; a FAT/exFAT flash drive can appear as a raw disk without being
mountable as ext2. The diagnostic output never formats a disk.

## Verification

`make test-usb-all` runs host HID/PS2 unit tests and QEMU tests for all four
controller types. Storage tests cover raw and MBR ext2, reads/writes, ELF loading,
simultaneous mouse traffic, mounted-device removal, reconnect generations,
unmount/flush, and host `e2fsck`/content verification. Pointer tests check buttons,
wheel, sustained reports through ring wrap, keyboard responsiveness and hotplug.
The xHCI hub configuration repeats the storage and pointer tests through a hub.

`make test-usb-companion` checks EHCI handoff to a UHCI companion.
`make test-usb-boot` installs to an xHCI flash image and boots it without the
installer CD on BIOS and UEFI, then verifies persisted data with `e2fsck`.

`USB_HOST=xhci make test-usb-mouse` exercises the installer and Paint on BIOS and
UEFI, including actual pointer clicks, saved BMP contents and unplug/replug.
`make test-storage-pci test-vfs` checks the shared disk namespace and filesystem
routing against IDE/SATA/NVMe/VirtIO behavior.

QEMU tests are regression evidence, not physical-hardware certification.

## Specifications

- [Intel xHCI requirements](https://www.intel.com/content/www/us/en/content-details/625472/extensible-host-controller-interface-for-universal-serial-bus-xhci-requirements-specification.html)
- [Intel EHCI specification](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/ehci-specification-for-usb.pdf)
- [USB-IF HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf)
