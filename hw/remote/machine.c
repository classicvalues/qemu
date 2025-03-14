/*
 * Machine for remote device
 *
 *  This machine type is used by the remote device process in multi-process
 *  QEMU. QEMU device models depend on parent busses, interrupt controllers,
 *  memory regions, etc. The remote machine type offers this environment so
 *  that QEMU device models can be used as remote devices.
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "hw/remote/machine.h"
#include "exec/memory.h"
#include "qapi/error.h"
#include "hw/pci/pci_host.h"
#include "hw/remote/iohub.h"

static bool remote_machine_get_bus(const char *type, BusState **bus,
                                   Error **errp)
{
    ERRP_GUARD();
    RemoteMachineState *s = REMOTE_MACHINE(current_machine);
    BusState *root_bus = NULL;
    PCIBus *new_pci_bus = NULL;

    if (!bus) {
        error_setg(errp, "Invalid argument");
        return false;
    }

    if (strcmp(type, TYPE_PCI_BUS) && strcmp(type, TYPE_PCI_BUS)) {
        return true;
    }

    root_bus = qbus_find_recursive(sysbus_get_default(), NULL, TYPE_PCIE_BUS);
    if (!root_bus) {
        error_setg(errp, "Unable to find root PCI device");
        return false;
    }

    new_pci_bus = pci_isol_bus_new(root_bus, type, errp);
    if (!new_pci_bus) {
        return false;
    }

    *bus = BUS(new_pci_bus);

    pci_bus_irqs(new_pci_bus, remote_iohub_set_irq, remote_iohub_map_irq,
                 &s->iohub, REMOTE_IOHUB_NB_PIRQS);

    return true;
}

static bool remote_machine_put_bus(BusState *bus, Error **errp)
{
    PCIBus *pci_bus = NULL;

    if (!bus) {
        error_setg(errp, "Invalid argument");
        return false;
    }

    if (!object_dynamic_cast(OBJECT(bus), TYPE_PCI_BUS)) {
        return true;
    }

    pci_bus = PCI_BUS(bus);

    return pci_isol_bus_free(pci_bus, errp);
}

static void remote_machine_init(MachineState *machine)
{
    MemoryRegion *system_memory, *system_io, *pci_memory;
    RemoteMachineState *s = REMOTE_MACHINE(machine);
    RemotePCIHost *rem_host;
    PCIHostState *pci_host;

    system_memory = get_system_memory();
    system_io = get_system_io();

    pci_memory = g_new(MemoryRegion, 1);
    memory_region_init(pci_memory, NULL, "pci", UINT64_MAX);

    rem_host = REMOTE_PCIHOST(qdev_new(TYPE_REMOTE_PCIHOST));

    rem_host->mr_pci_mem = pci_memory;
    rem_host->mr_sys_mem = system_memory;
    rem_host->mr_sys_io = system_io;

    s->host = rem_host;

    object_property_add_child(OBJECT(s), "remote-pcihost", OBJECT(rem_host));
    memory_region_add_subregion_overlap(system_memory, 0x0, pci_memory, -1);

    qdev_realize(DEVICE(rem_host), sysbus_get_default(), &error_fatal);

    pci_host = PCI_HOST_BRIDGE(rem_host);

    remote_iohub_init(&s->iohub);

    pci_bus_irqs(pci_host->bus, remote_iohub_set_irq, remote_iohub_map_irq,
                 &s->iohub, REMOTE_IOHUB_NB_PIRQS);

    qbus_set_hotplug_handler(BUS(pci_host->bus), OBJECT(s));

    qdev_set_bus_cbs(remote_machine_get_bus, remote_machine_put_bus,
                     &error_fatal);
}

static void remote_machine_pre_plug_cb(HotplugHandler *hotplug_dev,
                                       DeviceState *dev, Error **errp)
{
    if (!object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        error_setg(errp, "Only allowing PCI hotplug");
    }
}

static void remote_machine_unplug_cb(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp)
{
    if (!object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        error_setg(errp, "Only allowing PCI hot-unplug");
        return;
    }

    qdev_unrealize(dev);
}

static void remote_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    mc->init = remote_machine_init;
    mc->desc = "Experimental remote machine";

    hc->pre_plug = remote_machine_pre_plug_cb;
    hc->unplug = remote_machine_unplug_cb;
}

static const TypeInfo remote_machine = {
    .name = TYPE_REMOTE_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(RemoteMachineState),
    .class_init = remote_machine_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

static void remote_machine_register_types(void)
{
    type_register_static(&remote_machine);
}

type_init(remote_machine_register_types);
