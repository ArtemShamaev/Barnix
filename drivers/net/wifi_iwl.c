/* Intel Wi-Fi 6 cards of the iwlwifi "So/Ty/Ma" families (AX201/AX211
 * CNVi on Alder Lake laptops, AX210...). Written from Linux iwlwifi
 * (drivers/net/wireless/intel/iwlwifi, GPL-2.0 OR BSD-3-Clause,
 * Copyright Intel Corporation and Intel Mobile Communications GmbH), used
 * under the BSD-3-Clause terms.
 *
 * Stage 1: find the card, move its registers below 4 GiB, take ownership
 * from the platform (PCI_OWN_SET), wake the MAC (INIT_DONE ->
 * MAC_CLOCK_READY), read the hardware and RF ids, the radio kill switch,
 * and check the firmware file in /lib/firmware.
 * Stage 2: start the firmware the AX210-family way (pcie/ctxt-info-v2.c,
 * trans-gen2.c): the sections go to DRAM, the card's ROM loads them through
 * the context info and the image loader (IML), and the firmware reports
 * ALIVE on the default RX queue. Everything is polled, no interrupts.
 * `devices` shows how far it got. Commands, NVM and scanning come next. */
#include "wifi_iwl.h"
#include "storage_pci.h"
#include "barnix.h"
#include "fs.h"

#include "usb_time.h"
#include "memory.h"

/* registers (iwl-csr.h) */
#define CSR_HW_IF_CONFIG_REG 0x000
#define CSR_INT 0x008
#define CSR_INT_MASK 0x00c
#define CSR_FH_INT_STATUS 0x010
#define CSR_RESET 0x020
#define CSR_GP_CNTRL 0x024
#define CSR_HW_REV 0x028
#define CSR_MBOX_SET_REG 0x088
#define CSR_HW_RF_ID 0x09c
#define CSR_GIO_CHICKEN_BITS 0x100
#define CSR_DBG_HPET_MEM_REG 0x240
#define CSR_DBG_LINK_PWR_MGMT_REG 0x250
#define HW_IF_HAP_WAKE 0x00080000U
#define HW_IF_PCI_OWN_SET 0x00400000U
#define HW_IF_WAKE_ME 0x08000000U
#define MBOX_OS_ALIVE 0x20U
#define GP_MAC_CLOCK_READY 0x00000001U
#define GP_INIT_DONE 0x00000004U
#define GP_HW_RF_KILL_SW 0x08000000U
#define RESET_LINK_PWR_MGMT_DISABLED 0x80000000U
#define GIO_L1A_NO_L0S_RX 0x00800000U
#define HPET_MEM_VAL 0xffff0000U
#define CSR_INT_COALESCING 0x004
#define CSR_UCODE_DRV_GP1_CLR 0x05c
#define CSR_MAC_SHADOW_REG_CTRL 0x0a8
#define CSR_LTR_LAST_MSG 0x0dc
#define CSR_CTXT_INFO_ADDR 0x118
#define CSR_IML_DATA_ADDR 0x120
#define CSR_IML_SIZE_ADDR 0x128
#define CSR_IML_RESP_ADDR 0x12c
#define HBUS_TARG_PRPH_WADDR 0x444
#define HBUS_TARG_PRPH_RADDR 0x448
#define HBUS_TARG_PRPH_WDAT 0x44c
#define HBUS_TARG_PRPH_RDAT 0x450
#define RFH_Q0_FRBDCB_WIDX_TRG 0x1c80
#define CSR_MSIX_HW_INT_CAUSES_AD 0x2808
#define CSR_AUTO_FUNC_BOOT_ENA 0x00000002U   /* in CSR_CTXT_INFO_BOOT_CTRL = reg 0 */
#define GP_MAC_ACCESS_REQ 0x00000008U
#define GP_GOING_TO_SLEEP 0x00000010U
#define UCODE_SW_BIT_RFKILL 0x00000002U
#define UCODE_DRV_GP1_BIT_CMD_BLOCKED 0x00000004U
#define INT_BIT_ALIVE 0x00000001U
#define INT_BIT_SW_ERR 0x02000000U
#define INT_BIT_HW_ERR 0x20000000U
#define MSIX_HW_INT_CAUSES_REG_IML 0x00000002U
/* periphery registers, behind the UMAC offset of the So family (0x300000) */
#define UMAC_PRPH_OFFSET 0x300000U
#define UREG_CHICK 0xa05c00U
#define UREG_CHICK_MSI_ENABLE 0x01000000U
#define UREG_CPU_INIT_RUN 0xa05c44U
/* firmware file (fw/file.h) */
#define TLV_SEC_RT 19
#define TLV_SECURE_SEC_RT 24
#define TLV_IML 52
#define CPU1_CPU2_SEPARATOR 0xffffccccU
#define PAGING_SEPARATOR 0xaaaabbbbU
#define DRAM_ENTRIES 64
/* rings: the command queue of 128 TFDs of 256 bytes, RX of 512 descriptors */
#define CMD_QUEUE_SIZE 128
#define TFD_SIZE 256
#define RX_QUEUE_SIZE 512
#define RX_BUFFERS 64
#define RX_BUFFER_SIZE 4096
#define ALIVE_NOTIFICATION 0x01
#define ALIVE_STATUS_OK 0xcafe
/* prph scratch control flags (fw/api/ctxt-info-v2.h) */
#define SCRATCH_RB_SIZE_4K (1U << 16)
#define SCRATCH_MTR_MODE (1U << 17)
#define SCRATCH_MTR_FORMAT_256B 0x000c0000U

static const unsigned short ids[] = {0x2725, 0x7a70, 0x7af0, 0x51f0, 0x51f1, 0x54f0, 0x7f70, 0x2729, 0x7e40};
static const char *const firmware_names[] = {"so-a0-hr-b0", "so-a0-gf-a0", "ty-a0-gf-a0", "ma-b0-gf-a0"};

static struct {
    int found, owned, awake, rfkill, firmware_ok, alive;
    unsigned int pci, device, base, hw_rev, rf_id;
    int firmware_size;
    char firmware[80], firmware_version[68], problem[80];
    /* stage 2: what the start reached, and the registers when it stopped */
    const char *stage;
    unsigned int sections[3], int_status, boot_stage, iml_response, alive_status;
} card;

/* Device-visible memory: Barnix runs identity mapped below 4 GiB, so a
 * kernel block's address is its bus address. Blocks are never freed. */
static void *dma_zalloc(unsigned int bytes)
{
    void *block = memory_kernel_alloc(bytes);
    if (block) memset(block, 0, bytes);
    return block;
}
static void put16(unsigned char *p, unsigned int v) { p[0] = v; p[1] = v >> 8; }
static void put32(unsigned char *p, unsigned int v) { put16(p, v); put16(p + 2, v >> 16); }
static void put64(unsigned char *p, unsigned int v) { put32(p, v); put32(p + 4, 0); }
static unsigned int get32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned int)p[3] << 24; }

static unsigned int pci_read(unsigned int pci, unsigned int off)
{
    __asm__ volatile("outl %0,%1" :: "a"(0x80000000U | pci | (off & ~3U)), "Nd"((unsigned short)0xcf8));
    unsigned int v; __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"((unsigned short)0xcfc));
    return v;
}
static void pci_write16(unsigned int pci, unsigned int off, unsigned short v)
{
    __asm__ volatile("outl %0,%1" :: "a"(0x80000000U | pci | (off & ~3U)), "Nd"((unsigned short)0xcf8));
    __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"((unsigned short)(0xcfc + (off & 2))));
}
static unsigned int rd(unsigned int reg) { return *(volatile unsigned int *)(uintptr_t)(card.base + reg); }
static void wr(unsigned int reg, unsigned int v) { *(volatile unsigned int *)(uintptr_t)(card.base + reg) = v; }
static void set_bits(unsigned int reg, unsigned int mask) { wr(reg, rd(reg) | mask); }
/* 0 once (reg & mask) == mask, within about ms milliseconds */
static int poll(unsigned int reg, unsigned int mask, unsigned int ms)
{
    for (unsigned int t = 0; t <= ms; t++) {
        if ((rd(reg) & mask) == mask) return 0;
        usb_delay_ms(1);
    }
    return -1;
}
/* iwl_pcie_set_hw_ready / iwl_pcie_prepare_card_hw */
static int set_hw_ready(void)
{
    set_bits(CSR_HW_IF_CONFIG_REG, HW_IF_PCI_OWN_SET);
    if (poll(CSR_HW_IF_CONFIG_REG, HW_IF_PCI_OWN_SET, 1)) return -1;
    set_bits(CSR_MBOX_SET_REG, MBOX_OS_ALIVE);
    return 0;
}
static int prepare_card(void)
{
    if (!set_hw_ready()) return 0;
    set_bits(CSR_DBG_LINK_PWR_MGMT_REG, RESET_LINK_PWR_MGMT_DISABLED);
    usb_delay_ms(2);
    for (int i = 0; i < 10; i++) {
        set_bits(CSR_HW_IF_CONFIG_REG, HW_IF_WAKE_ME);
        for (unsigned int t = 0; t < 150; t++) {   /* 150 ms, as Linux */
            if (!set_hw_ready()) return 0;
            usb_delay_ms(1);
        }
        usb_delay_ms(25);
    }
    return -1;
}
/* iwl_pcie_gen2_apm_init + iwl_pcie_activate_nic */
static int wake(void)
{
    set_bits(CSR_GIO_CHICKEN_BITS, GIO_L1A_NO_L0S_RX);
    set_bits(CSR_DBG_HPET_MEM_REG, HPET_MEM_VAL);
    set_bits(CSR_HW_IF_CONFIG_REG, HW_IF_HAP_WAKE);
    set_bits(CSR_GP_CNTRL, GP_INIT_DONE);
    return poll(CSR_GP_CNTRL, GP_MAC_CLOCK_READY, 25);
}
/* The firmware: iwlwifi-NAME-VERSION.ucode, a TLV file ("IWL\n" magic). */
static void check_firmware(void)
{
    static unsigned char header[88];
    for (unsigned int n = 0; n < sizeof(firmware_names) / sizeof(firmware_names[0]); n++)
        for (int version = 99; version >= 59; version--) {
            char path[80] = "/lib/firmware/iwlwifi-", number[8];
            strcat(path, firmware_names[n]); strcat(path, "-");
            utoa((unsigned int)version, number); strcat(path, number); strcat(path, ".ucode");
            int size = fs_size(path);
            if (size <= 0) continue;
            strcpy(card.firmware, path);
            card.firmware_size = size;
            if (size < (int)sizeof(header) || fs_read(path, 0, header, sizeof(header)) != (int)sizeof(header)) return;
            unsigned int magic = header[4] | header[5] << 8 | header[6] << 16 | (unsigned int)header[7] << 24;
            if (header[0] || header[1] || header[2] || header[3] || magic != 0x0a4c5749U) return;
            unsigned int i = 0;
            for (; i < 64 && header[8 + i] && i + 1 < sizeof(card.firmware_version); i++) card.firmware_version[i] = (char)header[8 + i];
            card.firmware_version[i] = 0;
            card.firmware_ok = 1;
            return;
        }
}
/* iwl_trans_pcie_grab_nic_access: keep the MAC awake for periphery access. */
static int grab_nic(void)
{
    set_bits(CSR_GP_CNTRL, GP_MAC_ACCESS_REQ);
    usb_delay_ms(1);
    for (unsigned int t = 0; t < 15; t++) {
        if ((rd(CSR_GP_CNTRL) & (GP_MAC_CLOCK_READY | GP_GOING_TO_SLEEP)) == GP_MAC_CLOCK_READY) return 0;
        usb_delay_ms(1);
    }
    return -1;
}
static void release_nic(void) { wr(CSR_GP_CNTRL, rd(CSR_GP_CNTRL) & ~GP_MAC_ACCESS_REQ); }
static int write_umac_prph(unsigned int reg, unsigned int value)
{
    if (grab_nic()) return -1;
    wr(HBUS_TARG_PRPH_WADDR, ((reg + UMAC_PRPH_OFFSET) & 0x00ffffffU) | 3U << 24);
    wr(HBUS_TARG_PRPH_WDAT, value);
    release_nic();
    return 0;
}

/* The regular (runtime) image: LMAC sections, a separator, UMAC sections,
 * a separator, paging sections; each goes to its own DRAM block and its
 * address into the matching table of the prph scratch (iwl_pcie_init_fw_sec). */
static int load_sections(unsigned char *dram_common, unsigned char **iml, unsigned int *iml_size)
{
    static unsigned char header[88];
    if (fs_read(card.firmware, 0, header, sizeof(header)) != (int)sizeof(header)) return -1;
    unsigned int offset = sizeof(header), kind = 0, count[3] = {0, 0, 0};
    *iml = 0; *iml_size = 0;
    while (offset + 8 <= (unsigned int)card.firmware_size) {
        unsigned char tlv[12];
        if (fs_read(card.firmware, offset, tlv, 12) != 12) return -1;
        unsigned int type = get32(tlv), length = get32(tlv + 4), data = offset + 8;
        if (length > (unsigned int)card.firmware_size - data) return -1;
        if (type == TLV_IML && !*iml) {
            if (!(*iml = dma_zalloc(length)) || fs_read(card.firmware, data, *iml, length) != (int)length) return -1;
            *iml_size = length;
        } else if ((type == TLV_SEC_RT || type == TLV_SECURE_SEC_RT) && length >= 4) {
            unsigned int load = get32(tlv + 8);
            if (load == CPU1_CPU2_SEPARATOR || load == PAGING_SEPARATOR) { if (kind < 2) kind++; }
            else {
                if (count[kind] == DRAM_ENTRIES) return -1;
                unsigned char *block = dma_zalloc(length - 4);
                if (!block || fs_read(card.firmware, data + 4, block, length - 4) != (int)(length - 4)) return -1;
                /* dram.common: umac_img[64], lmac_img[64], virtual_img[64] */
                unsigned int table = kind == 0 ? 1 : kind == 1 ? 0 : 2;
                put64(dram_common + (table * DRAM_ENTRIES + count[kind]) * 8, (unsigned int)(uintptr_t)block);
                count[kind]++;
            }
        }
        offset = data + ((length + 3) & ~3U);
    }
    for (int i = 0; i < 3; i++) card.sections[i] = count[i];
    return count[0] && count[1] && *iml ? 0 : -1;
}

/* iwl_trans_pcie_gen2_start_fw for the AX210 family, up to ALIVE. */
static void start_firmware(void)
{
    card.stage = "registers";
    wr(CSR_INT, 0xffffffffU);
    wr(CSR_UCODE_DRV_GP1_CLR, UCODE_SW_BIT_RFKILL);
    wr(CSR_UCODE_DRV_GP1_CLR, UCODE_DRV_GP1_BIT_CMD_BLOCKED);
    wr(CSR_INT, 0xffffffffU);
    if (write_umac_prph(UREG_CHICK, UREG_CHICK_MSI_ENABLE)) return;
    *(volatile unsigned char *)(uintptr_t)(card.base + CSR_INT_COALESCING) = 0x40;

    card.stage = "memory";
    /* RX: free descriptors {rbid, 0, addr}, completions of 32 bytes, status */
    unsigned char *free_ring = dma_zalloc(RX_QUEUE_SIZE * 16), *used_ring = dma_zalloc(RX_QUEUE_SIZE * 32);
    volatile unsigned short *status = dma_zalloc(4096);
    unsigned char *buffers = dma_zalloc(RX_BUFFERS * RX_BUFFER_SIZE), *tfds = dma_zalloc(CMD_QUEUE_SIZE * TFD_SIZE);
    unsigned char *scratch = dma_zalloc(4096), *info = dma_zalloc(4096), *context = dma_zalloc(4096);
    if (!free_ring || !used_ring || !status || !buffers || !tfds || !scratch || !info || !context) return;
    for (unsigned int i = 0; i < RX_BUFFERS; i++) {
        put16(free_ring + i * 16, i + 1);
        put64(free_ring + i * 16 + 8, (unsigned int)(uintptr_t)(buffers + i * RX_BUFFER_SIZE));
    }
    wr(CSR_MAC_SHADOW_REG_CTRL, rd(CSR_MAC_SHADOW_REG_CTRL) | 0x800fffffU);

    card.stage = "firmware sections";
    unsigned char *iml; unsigned int iml_size;
    /* prph scratch: ctrl_cfg (84 bytes), fseq_override, step_analog_params,
     * reserved[8], then dram.common at 124; the fseq table is left out. */
    if (load_sections(scratch + 124, &iml, &iml_size)) return;
    put16(scratch + 0, card.hw_rev & 0xffff);            /* version.mac_id */
    put16(scratch + 4, (124 + 3 * DRAM_ENTRIES * 8 + 64) / 4);
    put32(scratch + 8, SCRATCH_RB_SIZE_4K | SCRATCH_MTR_MODE | SCRATCH_MTR_FORMAT_256B);
    put64(scratch + 48, (unsigned int)(uintptr_t)free_ring);         /* rbd_cfg.free_rbd_addr */
    /* context info v2 (IPC_CONTEXT_INFO_S) */
    put64(context + 8, (unsigned int)(uintptr_t)info);               /* prph_info_base_addr */
    put64(context + 16, (unsigned int)(uintptr_t)status);            /* cr_head_idx_arr */
    put64(context + 24, (unsigned int)(uintptr_t)info + 2048);       /* tr_tail_idx_arr */
    put64(context + 32, (unsigned int)(uintptr_t)info + 3072);       /* cr_tail_idx_arr */
    put64(context + 52, (unsigned int)(uintptr_t)tfds);              /* mtr_base_addr */
    put64(context + 60, (unsigned int)(uintptr_t)used_ring);         /* mcr_base_addr */
    put16(context + 68, 7 - 3);                           /* mtr_size: log2(128) - 3 */
    put16(context + 70, 9);                               /* mcr_size: log2(512) */
    put64(context + 88, (unsigned int)(uintptr_t)scratch);           /* prph_scratch_base_addr */
    put32(context + 96, 124 + 3 * DRAM_ENTRIES * 8);      /* without the fseq table */

    card.stage = "image loader";
    wr(CSR_CTXT_INFO_ADDR, (unsigned int)(uintptr_t)context); wr(CSR_CTXT_INFO_ADDR + 4, 0);
    wr(CSR_IML_DATA_ADDR, (unsigned int)(uintptr_t)iml); wr(CSR_IML_DATA_ADDR + 4, 0);
    wr(CSR_IML_SIZE_ADDR, iml_size);
    set_bits(CSR_HW_IF_CONFIG_REG, CSR_AUTO_FUNC_BOOT_ENA);
    /* Integrated AX210-family parts: the ROM sets a wrong LTR, so keep the
     * device busy until the image loader is done (iwl_pcie_spin_for_iml). */
    wr(CSR_MSIX_HW_INT_CAUSES_AD, MSIX_HW_INT_CAUSES_REG_IML);
    if (write_umac_prph(UREG_CPU_INIT_RUN, 1)) return;
    for (unsigned int i = 0; i < 200000 && !(rd(CSR_MSIX_HW_INT_CAUSES_AD) & MSIX_HW_INT_CAUSES_REG_IML); i++)
        (void)rd(CSR_LTR_LAST_MSG);

    card.stage = "waiting for ALIVE";
    /* ALIVE shows in CSR_INT (MSI mode) or, if the card stayed in MSI-X
     * mode, among the MSI-X hardware causes. */
    for (unsigned int t = 0; t < 2000 && !(rd(CSR_INT) & (INT_BIT_ALIVE | INT_BIT_SW_ERR | INT_BIT_HW_ERR)) &&
                              !(rd(CSR_MSIX_HW_INT_CAUSES_AD) & INT_BIT_ALIVE); t++)
        usb_delay_ms(1);
    card.int_status = rd(CSR_INT) | (rd(CSR_MSIX_HW_INT_CAUSES_AD) & INT_BIT_ALIVE);
    card.boot_stage = get32(info);
    card.iml_response = rd(CSR_IML_RESP_ADDR);
    if (!(card.int_status & INT_BIT_ALIVE)) return;
    /* The firmware configured the RX hardware: give it the buffers. */
    wr(CSR_INT, INT_BIT_ALIVE); wr(CSR_MSIX_HW_INT_CAUSES_AD, INT_BIT_ALIVE);
    wr(RFH_Q0_FRBDCB_WIDX_TRG, RX_BUFFERS & ~7U);

    card.stage = "reading ALIVE";
    for (unsigned int t = 0; t < 1000 && !*status; t++) usb_delay_ms(1);
    if (!*status) return;
    unsigned int rbid = used_ring[4] | used_ring[5] << 8;
    if (!rbid || rbid > RX_BUFFERS) return;
    const unsigned char *packet = buffers + (rbid - 1) * RX_BUFFER_SIZE;
    card.alive_status = packet[8] | packet[9] << 8;
    if (packet[4] != ALIVE_NOTIFICATION) return;
    card.alive = card.alive_status == ALIVE_STATUS_OK;
    card.stage = card.alive ? "alive" : "ALIVE with an error status";
}

void wifi_iwl_init(void)
{
    if (card.found) return;
    for (unsigned int bus = 0; bus < 256 && !card.found; bus++)
        for (unsigned int slot = 0; slot < 32 && !card.found; slot++)
            for (unsigned int fn = 0; fn < 8; fn++) {
                unsigned int pci = bus << 16 | slot << 11 | fn << 8, id = pci_read(pci, 0);
                if ((id & 0xffff) == 0xffff) { if (!fn) break; continue; }
                if ((id & 0xffff) == 0x8086)
                    for (unsigned int k = 0; k < sizeof(ids) / sizeof(ids[0]); k++)
                        if (id >> 16 == ids[k]) { card.found = 1; card.pci = pci; card.device = id >> 16; }
                if (card.found || (!fn && !(pci_read(pci, 12) & 0x800000))) break;
            }
    if (!card.found) return;
    check_firmware();
    unsigned int bar = storage_pci_mmio_bar(card.pci, 0);   /* moved below 4 GiB when needed */
    if (!bar || (bar & 1)) { strcpy(card.problem, "registers above 4 GiB could not be moved"); return; }
    card.base = bar & ~15U;
    pci_write16(card.pci, 4, (unsigned short)(pci_read(card.pci, 4) | 6));   /* memory space, bus master */
    if (rd(CSR_HW_REV) == 0xffffffffU) { strcpy(card.problem, "the card does not answer (powered off?)"); return; }
    wr(CSR_INT_MASK, 0);                 /* polled: no interrupts */
    wr(CSR_INT, 0xffffffffU); wr(CSR_FH_INT_STATUS, 0xffffffffU);
    if (prepare_card()) { strcpy(card.problem, "the platform (CSME) keeps the card"); }
    else card.owned = 1;
    card.awake = card.owned && !wake();
    if (card.owned && !card.awake) strcpy(card.problem, "the MAC clock did not start");
    card.hw_rev = rd(CSR_HW_REV);
    card.rf_id = rd(CSR_HW_RF_ID);
    card.rfkill = !(rd(CSR_GP_CNTRL) & GP_HW_RF_KILL_SW);
    if (card.awake && card.firmware_ok && !card.rfkill) start_firmware();
}
int wifi_iwl_present(void) { return card.found; }
static void hex(unsigned int value, int digits)
{
    char text[12];
    for (int i = 0; i < digits; i++) text[i] = "0123456789abcdef"[(value >> ((digits - 1 - i) * 4)) & 15];
    text[digits] = 0;
    print(WHITE, text);
}
void wifi_iwl_report(void)
{
    wifi_iwl_init();
    if (!card.found) return;
    print(WHITE, "wifi: Intel 8086:"); hex(card.device, 4);
    print(WHITE, " hw "); hex(card.hw_rev, 8); print(WHITE, " rf "); hex(card.rf_id, 8);
    print(WHITE, card.awake ? " awake" : card.owned ? " owned" : " not ready");
    if (card.rfkill) print(YELLOW, " (radio switched off)");
    println(WHITE, "");
    if (card.problem[0]) { print(RED, "      problem: "); println(RED, card.problem); }
    print(WHITE, "      firmware: ");
    if (!card.firmware[0]) println(YELLOW, "missing (/lib/firmware/iwlwifi-so-a0-hr-b0-*.ucode)");
    else { print(WHITE, card.firmware + 14); print(WHITE, " "); println(card.firmware_ok ? GREEN : RED, card.firmware_ok ? card.firmware_version : "damaged"); }
    if (card.stage) {
        print(WHITE, "      start: ");
        print(card.alive ? GREEN : RED, card.alive ? "firmware is running (ALIVE)" : card.stage);
        print(WHITE, "  sections "); hex(card.sections[0], 2); print(WHITE, "/"); hex(card.sections[1], 2);
        print(WHITE, "/"); hex(card.sections[2], 2); println(WHITE, "");
        print(WHITE, "      int "); hex(card.int_status, 8); print(WHITE, " boot "); hex(card.boot_stage, 8);
        print(WHITE, " iml "); hex(card.iml_response, 8); print(WHITE, " alive "); hex(card.alive_status, 4);
        println(WHITE, "");
    }
    println(DARK_GREY, card.alive ? "      driver stage 2 of 4: the firmware runs; scanning and connecting come later"
                                  : "      driver stage 2 of 4: starting the firmware; connecting comes later");
}
