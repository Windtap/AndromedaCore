#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/core/qdev-properties.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "system/memory.h"

#define TYPE_FASTPIPE "fastpipe"
OBJECT_DECLARE_SIMPLE_TYPE(FastpipeState, FASTPIPE)

#define FASTPIPE_VENDOR_ID 0x80EE
#define FASTPIPE_DEVICE_ID 0xDBDB

#define PIPE_COUNT 1024
#define PIPE_SIZE 48

/* Structure in BAR1 memory representing a pipe */
#define SERIAL_CALL_OFFSET 115036
#define SERIAL_CALL_ID_OFFSET (SERIAL_CALL_OFFSET + 115036 - 115036) /* Correcting offsets based on sendSerialCallToHost.c */
/* Based on sendSerialCallToHost.c:
   v10 = g_pPipeMem;
   *(_DWORD *)(g_pPipeMem + 115036) = 0; // ready flag
   v10[28758] = a1; // cmd id (offset 115032)
   memcpy(v10 + 28761, a2, a3); // data (offset 115044)
   v10[28760] = a3; // len (offset 115040)
*/

#define SERIAL_CMD_OFFSET 115032
#define SERIAL_READY_OFFSET 115036
#define SERIAL_LEN_OFFSET 115040
#define SERIAL_DATA_OFFSET 115044

struct FastpipePipe {
    uint32_t lock;      /* offset 0 */
    uint32_t main_lock; /* offset 8 */
    uint32_t state;     /* offset 40, 1 = eUsing, 2 = eClosing */
    uint32_t cookie;    /* offset 44 */
    uint32_t type;      /* offset 52 */
};

struct FastpipeState {
    PCIDevice parent_obj;

    MemoryRegion bar0_io;      /* I/O ports for commands */
    MemoryRegion bar1_mem;     /* Pipe shared memory (32MB) */
    MemoryRegion bar2_control;  /* Control set (64KB) */
    MemoryRegion bar3_blob;     /* Dynamic Blob (1.5GB) */
    MemoryRegion bar4_vram;     /* VRAM/Other (64MB) */
    
    uint32_t status;
    uint32_t irq_status;
    uint32_t last_cmd;
    
    uint8_t *bar1_ptr;
};

static uint64_t fastpipe_io_read(void *opaque, hwaddr addr, unsigned size)
{
    FastpipeState *s = opaque;
    uint64_t val = 0;
    switch (addr) {
    case 0x0: /* Status register */
        val = s->status;
        break;
    case 0x4: /* Interrupt status */
        val = s->irq_status;
        break;
    case 0x8: /* Command/Last Cmd info */
        val = s->last_cmd;
        break;
    default:
        val = 0;
        break;
    }
    fprintf(stderr, "fastpipe: IO read addr:0x%lx val:0x%lx size:%u\n", (unsigned long)addr, (unsigned long)val, size);
    return val;
}

static void fastpipe_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    FastpipeState *s = opaque;
    fprintf(stderr, "fastpipe: IO write addr:0x%lx val:0x%lx size:%u\n", (unsigned long)addr, (unsigned long)val, size);
    switch (addr) {
    case 0x0: /* Command register (Found in logs: addr 0x0 is used for commands) */
        {
            uint32_t cmd = val & 0xFF;
            s->last_cmd = val;

            if (s->bar1_ptr) {
                switch (cmd) {
                case 1: /* Connect Pipe */
                    {
                        uint32_t r_id = (val >> 20) & 0xFFF;
                        uint32_t w_id = (val >> 8) & 0xFFF;
                        
                        fprintf(stderr, "fastpipe: Connect pipe R:%d W:%d (val:0x%08x)\n", r_id, w_id, (uint32_t)val);
                        
                        if (r_id < PIPE_COUNT && w_id < PIPE_COUNT) {
                            uint8_t *r_ptr = s->bar1_ptr + (r_id * PIPE_SIZE);
                            uint8_t *w_ptr = s->bar1_ptr + (w_id * PIPE_SIZE);
                            
                            /* Initialize read pipe */
                            *(uint32_t *)(r_ptr + 40) = 1; /* state = eUsing */
                            *(uint32_t *)(r_ptr + 24) = 1; /* type = non-zero, critical for my_condition_connect */
                            *(uint32_t *)(r_ptr + 44) = 10; /* Initial cookie/status sync */
                            
                            /* Initialize write pipe */
                            *(uint32_t *)(w_ptr + 40) = 1; /* state = eUsing */
                            *(uint32_t *)(w_ptr + 24) = 1; /* type = non-zero, critical for my_condition_connect */
                            *(uint32_t *)(w_ptr + 44) = 10; /* Initial cookie/status sync */
                        }
                        s->status |= 0x1; 

                        /* Force interrupt to notify guest that connect is "complete" */
                        s->irq_status |= 0x1;
                        if (msi_enabled(&s->parent_obj)) {
                            msi_notify(&s->parent_obj, 0);
                        } else {
                            pci_set_irq(&s->parent_obj, 1);
                        }
                    }
                    break;
                case 7: /* Serial Call */
                    {
                        /* Handle sync call from guest */
                        uint32_t sc_cmd = *(uint32_t *)(s->bar1_ptr + SERIAL_CMD_OFFSET);
                        uint32_t sc_len = *(uint32_t *)(s->bar1_ptr + SERIAL_LEN_OFFSET);
                        
                        fprintf(stderr, "fastpipe: Serial Call cmd:%u len:%u\n", sc_cmd, sc_len);
                        
                        /* For now, just mark as finished. In future we will handle graphics commands here. */
                        *(uint32_t *)(s->bar1_ptr + SERIAL_READY_OFFSET) = 1;
                        
                        /* Notify guest */
                        s->irq_status |= 0x1;
                        if (msi_enabled(&s->parent_obj)) {
                            msi_notify(&s->parent_obj, 0);
                        } else {
                            pci_set_irq(&s->parent_obj, 1);
                        }
                    }
                    break;
                default:
                    break;
                }
            }
        }
        break;
    case 0x4: /* Interrupt ACK */
    default:
        break;
    }
}

static const MemoryRegionOps fastpipe_io_ops = {
    .read = fastpipe_io_read,
    .write = fastpipe_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void fastpipe_realize(PCIDevice *pci_dev, Error **errp)
{
    FastpipeState *s = FASTPIPE(pci_dev);
    uint32_t *control_ptr;

    /* BAR 0: I/O (16 bytes) */
    memory_region_init_io(&s->bar0_io, OBJECT(s), &fastpipe_io_ops, s, "fastpipe-io", 16);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar0_io);

    /* BAR 1: Shared Memory (32MB) */
    if (!memory_region_init_ram(&s->bar1_mem, OBJECT(s), "fastpipe-mem", 32 * 1024 * 1024, errp)) {
        return;
    }
    pci_register_bar(pci_dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar1_mem);
    s->bar1_ptr = memory_region_get_ram_ptr(&s->bar1_mem);

    /* BAR 2: Control Set (64KB) */
    if (!memory_region_init_ram(&s->bar2_control, OBJECT(s), "fastpipe-control", 64 * 1024, errp)) {
        return;
    }
    pci_register_bar(pci_dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar2_control);

    /* Initialize Control Set version */
    /* Driver expects version >= 1 at the beginning of BAR2 */
    control_ptr = memory_region_get_ram_ptr(&s->bar2_control);
    if (control_ptr) {
        control_ptr[0] = 1; /* version */
        control_ptr[1] = 1; /* properties count */
        fprintf(stderr, "fastpipe: Initialized BAR2 version to %u\n", control_ptr[0]);
    }

    /* BAR 3: Dynamic Blob (512MB) */
    /* Reduced from 2GB to fit in 32-bit PCI hole, as guest kernel failed to assign 2GB */
    if (!memory_region_init_ram(&s->bar3_blob, OBJECT(s), "fastpipe-blob", 512ULL * 1024 * 1024, errp)) {
        return;
    }
    pci_register_bar(pci_dev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar3_blob);

    /* BAR 4: VRAM/Extra (64MB) */
    if (!memory_region_init_ram(&s->bar4_vram, OBJECT(s), "fastpipe-vram", 64 * 1024 * 1024, errp)) {
        return;
    }
    pci_register_bar(pci_dev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar4_vram);

    /* LDPlayer driver expect device at 00:04.0 or similar with specific ID */
    /* Initialize MSI and IRQ */
    pci_config_set_interrupt_pin(pci_dev->config, 1);
    if (msi_init(pci_dev, 0, 1, true, false, errp)) {
        return;
    }

    /* Force Vendor/Device ID to match LDPlayer's expected values */
    pci_config_set_vendor_id(pci_dev->config, FASTPIPE_VENDOR_ID);
    pci_config_set_device_id(pci_dev->config, FASTPIPE_DEVICE_ID);
    pci_config_set_class(pci_dev->config, PCI_CLASS_OTHERS << 8);

    /* Set Subsystem ID to 0:0 to match VirtualBox default */
    pci_set_word(pci_dev->config + PCI_SUBSYSTEM_VENDOR_ID, 0x0000);
    pci_set_word(pci_dev->config + PCI_SUBSYSTEM_ID, 0x0000);

    /* Initial status: ready */
    s->status = 0x1;
}

static void fastpipe_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(oc);

    k->realize = fastpipe_realize;
    k->vendor_id = FASTPIPE_VENDOR_ID;
    k->device_id = FASTPIPE_DEVICE_ID;
    k->class_id = PCI_CLASS_OTHERS;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo fastpipe_types[] = {
    {
        .name          = TYPE_FASTPIPE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(FastpipeState),
        .class_init    = fastpipe_class_init,
        .interfaces = (const InterfaceInfo[]) {
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            { },
        },
    },
};

DEFINE_TYPES(fastpipe_types)
