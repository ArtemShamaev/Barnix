#ifndef STORAGE_PCI_H
#define STORAGE_PCI_H
#include <stdint.h>
#include "multiboot.h"
void storage_pci_boot_memory(const MultibootInfo *info);
/* Shared PCI MMIO allocator, also used by USB host controllers. */
unsigned int storage_pci_mmio_bar(unsigned int pci, unsigned int bar);
#define PCI_STORAGE_MAX 32
void storage_pci_init(void);
int storage_pci_count(void);
const char *storage_pci_name(int index);
const char *storage_pci_type(int index);
uint64_t storage_pci_capacity(int index);
int storage_pci_present(int index);
int storage_pci_read(int index, unsigned int lba, void *data, unsigned int sectors);
int storage_pci_write(int index, unsigned int lba, const void *data, unsigned int sectors);
int storage_pci_flush(int index);
#endif
