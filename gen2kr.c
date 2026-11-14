/*
 * NVIDIA P1xx/CMP tool to force retrain GPUs into PCI Express Gen2 Mode, adapted from:
 * https://github.com/WJNKAC/nvidia-p104-unlock-gen2-for-linux/tree/19d59424ec9d69fe0283706e1ce4e7d70fd258fb
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/workqueue.h>

#define BAR_INDEX               0
#define BAR_LENGTH              0x90000
#define XP3G_OVERRIDE0          0x8e110
#define XP3G_OVERRIDE3          0x8e11c
#define XP3G_VALUE0             0x8e120
#define XP3G_VALUE3             0x8e12c
#define PRIV_MISC1              0x8841c
#define VSEC_HIERARCHY          0x88610
#define LINK_CONFIG0            0x8c040
#define CYA0                    0x8c2c0
#define XVE_OVERRIDE            0x8872c
#define OVERRIDE0_ENABLE        BIT(0)
#define OVERRIDE3_ENABLE        BIT(2)
#define VALUE3_GEN2             BIT(21)
#define PRIV_ENABLE            (BIT(11) | BIT(13))
#define PASCAL_PRIV_ENABLE     (PRIV_ENABLE | BIT(15))
#define PRIV_DISABLE           (BIT(12) | BIT(14))
#define VSEC_ENABLE            BIT(0)
#define VSEC_DISABLE           BIT(12)
#define DISABLE_GEN2            BIT(2)
#define LINK_COMMIT             BIT(0)
#define LINK_SPEED_MASK         GENMASK(19, 18)
#define PASCAL_LINK_SPEED_GEN2  BIT(18)
#define CMP_LINK_SPEED_GEN2     BIT(19)
#define XVE_TRAIN_GEN2          0x6
#define INVALID_REGISTER        U32_MAX
#define BLOCKED_REGISTER_MASK   GENMASK(31, 16)
#define BLOCKED_REGISTER        0xbadf0000
#define TRAIN_DELAY_MS          50
#define POLL_DELAY_MS           100
#define POLL_ATTEMPTS           20
#define PROBE_INSTANCES         16

static char *target_bdf;
module_param(target_bdf, charp, 0444);
MODULE_PARM_DESC(target_bdf, "P1xx/CMP PCI address in domain:bus:slot.function form");

struct gen2_profile {
    u32 link_speed;
    u32 priv_enable;
};

static const struct gen2_profile pascal_profile = {
    .link_speed = PASCAL_LINK_SPEED_GEN2,
    .priv_enable = PASCAL_PRIV_ENABLE,
};

static const struct gen2_profile cmp_profile = {
    .link_speed = CMP_LINK_SPEED_GEN2,
    .priv_enable = PRIV_ENABLE,
};

static const struct pci_device_id gen2_devices[] = {
    { PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x1b07),
      .driver_data = (kernel_ulong_t)&pascal_profile }, /* P102-100 */
    { PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x1b87),
      .driver_data = (kernel_ulong_t)&pascal_profile }, /* P104-100 */
    { PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x1bc7),
      .driver_data = (kernel_ulong_t)&pascal_profile }, /* P104-101 */
    { PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x1c07),
      .driver_data = (kernel_ulong_t)&pascal_profile }, /* P106-100 */
    { PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x1c09),
      .driver_data = (kernel_ulong_t)&pascal_profile }, /* P106-090 */
    { PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x2189),
      .driver_data = (kernel_ulong_t)&cmp_profile }, /* CMP 30HX */
    { PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x1f0b),
      .driver_data = (kernel_ulong_t)&cmp_profile }, /* CMP 40HX */
    { PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x1e09),
      .driver_data = (kernel_ulong_t)&cmp_profile }, /* CMP 50HX */
    { PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x248a),
      .driver_data = (kernel_ulong_t)&cmp_profile }, /* CMP 70HX */
    { PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x220d),
      .driver_data = (kernel_ulong_t)&cmp_profile }, /* CMP 90HX */
    { PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x2082),
      .driver_data = (kernel_ulong_t)&cmp_profile }, /* CMP 170HX */
    { PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x20c2),
      .driver_data = (kernel_ulong_t)&cmp_profile }, /* CMP 170HX */
    { }
};

static struct pci_dev *target;
static const struct gen2_profile *profile;
static void __iomem *bar;
static unsigned int triggered;
module_param_named(probe_triggered, triggered, uint, 0444);
MODULE_PARM_DESC(probe_triggered, "0=waiting for the selected GPU probe, 1=probe handled");

static bool invalid_register(u32 value)
{
    return value == INVALID_REGISTER ||
           (value & BLOCKED_REGISTER_MASK) == BLOCKED_REGISTER;
}

static void retrain(struct work_struct *work)
{
    struct pci_dev *up = pci_upstream_bridge(target);
    u32 config, vsec, cya, link;
    u16 status;
    int attempt;

    config = readl(bar + PRIV_MISC1);
    vsec = readl(bar + VSEC_HIERARCHY);
    cya = readl(bar + CYA0);
    link = readl(bar + LINK_CONFIG0);
    if (invalid_register(config) || invalid_register(vsec) ||
        invalid_register(cya) || invalid_register(link) ||
        (link & LINK_COMMIT)) {
        dev_err(&target->dev, "gen2kr: invalid BAR0 state; retrain aborted\n");
        return;
    }

    writel(0, bar + XP3G_VALUE0);
    writel(VALUE3_GEN2, bar + XP3G_VALUE3);
    writel(OVERRIDE0_ENABLE, bar + XP3G_OVERRIDE0);
    writel(OVERRIDE3_ENABLE, bar + XP3G_OVERRIDE3);
    writel((config | profile->priv_enable) & ~PRIV_DISABLE, bar + PRIV_MISC1);
    writel((vsec & ~VSEC_DISABLE) | VSEC_ENABLE, bar + VSEC_HIERARCHY);
    writel(cya & ~DISABLE_GEN2, bar + CYA0);

    link = readl(bar + LINK_CONFIG0);
    if (invalid_register(link) || (link & LINK_COMMIT)) {
        dev_err(&target->dev, "gen2kr: invalid link state; retrain aborted\n");
        return;
    }
    writel((link & ~LINK_SPEED_MASK) | profile->link_speed, bar + LINK_CONFIG0);
    link = readl(bar + LINK_CONFIG0);
    config = readl(bar + PRIV_MISC1);
    vsec = readl(bar + VSEC_HIERARCHY);
    cya = readl(bar + CYA0);
    if (invalid_register(link) || invalid_register(config) ||
        invalid_register(vsec) || invalid_register(cya) ||
        (link & LINK_COMMIT) ||
        (link & LINK_SPEED_MASK) != profile->link_speed ||
        (config & (profile->priv_enable | PRIV_DISABLE)) != profile->priv_enable ||
        (vsec & (VSEC_ENABLE | VSEC_DISABLE)) != VSEC_ENABLE ||
        (cya & DISABLE_GEN2)) {
        dev_err(&target->dev, "gen2kr: Gen2 register writes rejected; check device privileges\n");
        return;
    }
    writel(XVE_TRAIN_GEN2, bar + XVE_OVERRIDE);
    msleep(TRAIN_DELAY_MS);

    if (pcie_capability_clear_and_set_word(target, PCI_EXP_LNKCTL2,
                                         PCI_EXP_LNKCTL2_TLS,
                                         PCI_EXP_LNKCTL2_TLS_5_0GT) ||
        pcie_capability_clear_and_set_word(up, PCI_EXP_LNKCTL2,
                                         PCI_EXP_LNKCTL2_TLS,
                                         PCI_EXP_LNKCTL2_TLS_5_0GT) ||
        pcie_capability_set_word(up, PCI_EXP_LNKCTL, PCI_EXP_LNKCTL_RL)) {
        dev_err(&target->dev, "gen2kr: PCIe configuration failed\n");
        return;
    }

    for (attempt = 0; attempt < POLL_ATTEMPTS; attempt++) {
        msleep(POLL_DELAY_MS);
        if (!pcie_capability_read_word(target, PCI_EXP_LNKSTA, &status) &&
            status != U16_MAX &&
            !(status & PCI_EXP_LNKSTA_LT) &&
            (status & PCI_EXP_LNKSTA_CLS) >= PCI_EXP_LNKSTA_CLS_5_0GB) {
            dev_info(&target->dev, "gen2kr: Gen2 retrain completed, LNKSTA=%04x\n",
                     status);
            return;
        }
    }
    dev_warn(&target->dev, "gen2kr: Gen2 negotiation timed out\n");
}

static DECLARE_WORK(retrain_work, retrain);

static int probe_entry(struct kretprobe_instance *instance, struct pt_regs *regs)
{
    struct pci_dev *device = (struct pci_dev *)regs_get_kernel_argument(regs, 0);

    return device != target || READ_ONCE(triggered);
}

static int probe_return(struct kretprobe_instance *instance, struct pt_regs *regs)
{
    u32 link;

    if (regs_return_value(regs) || cmpxchg(&triggered, 0, 1))
        return 0;

    link = readl(bar + LINK_CONFIG0);
    if (invalid_register(link) || (link & LINK_COMMIT)) {
        dev_err(&target->dev, "gen2kr: invalid probe-time link state\n");
        return 0;
    }
    writel((link & ~LINK_SPEED_MASK) | profile->link_speed, bar + LINK_CONFIG0);
    link = readl(bar + LINK_CONFIG0);
    if (invalid_register(link) || (link & LINK_COMMIT)) {
        dev_err(&target->dev, "gen2kr: invalid probe-time speed readback\n");
        return 0;
    }
    if ((link & LINK_SPEED_MASK) == profile->link_speed)
        writel(XVE_TRAIN_GEN2, bar + XVE_OVERRIDE);
    else
        dev_warn(&target->dev, "gen2kr: applying capability overrides before retraining\n");
    schedule_work(&retrain_work);
    return 0;
}

static struct kretprobe probe = {
    .kp.symbol_name = "nv_pci_probe",
    .entry_handler = probe_entry,
    .handler = probe_return,
    .maxactive = PROBE_INSTANCES,
};

static int __init gen2_init(void)
{
    struct pci_dev *device = NULL;
    const struct pci_device_id *id;
    int error;

    if (!target_bdf || !*target_bdf) {
        pr_err("gen2kr: target_bdf=<PCI-address> is required\n");
        return -EINVAL;
    }

    while ((device = pci_get_device(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID, device))) {
        if (!strcmp(pci_name(device), target_bdf)) {
            target = device;
            break;
        }
    }
    if (!target) {
        pr_err("gen2kr: NVIDIA PCI device %s was not found\n", target_bdf);
        return -ENODEV;
    }
    id = pci_match_id(gen2_devices, target);
    if (!id) {
        dev_err(&target->dev, "gen2kr: unsupported P1xx/CMP device ID %04x\n",
                target->device);
        error = -ENODEV;
        goto put_device;
    }
    profile = (const struct gen2_profile *)id->driver_data;
    if (!pci_is_pcie(target) || !pci_upstream_bridge(target) ||
        !(pci_resource_flags(target, BAR_INDEX) & IORESOURCE_MEM) ||
        pci_resource_len(target, BAR_INDEX) < BAR_LENGTH) {
        error = -ENODEV;
        goto put_device;
    }

    bar = ioremap(pci_resource_start(target, BAR_INDEX), BAR_LENGTH);
    if (!bar) {
        dev_err(&target->dev, "gen2kr: BAR0 mapping failed\n");
        error = -ENOMEM;
        goto put_device;
    }
    error = register_kretprobe(&probe);
    if (error) {
        dev_err(&target->dev, "gen2kr: return probe registration for %s failed: %d\n",
                probe.kp.symbol_name, error);
        goto unmap;
    }

    dev_info(&target->dev, "gen2kr: waiting for NVIDIA probe\n");
    return 0;

unmap:
    iounmap(bar);
put_device:
    pci_dev_put(target);
    return error;
}

static void __exit gen2_exit(void)
{
    unregister_kretprobe(&probe);
    cancel_work_sync(&retrain_work);
    iounmap(bar);
    pci_dev_put(target);
}

module_init(gen2_init);
module_exit(gen2_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Tool to retrain CMP/P1xx series NVIDIA GPUs into PCI Express Gen2");
MODULE_SOFTDEP("pre: nvidia");
