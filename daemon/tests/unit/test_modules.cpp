#include "bus/bus_util.h"
#include "modules/battery.h"
#include "modules/cpu.h"
#include "modules/disk.h"
#include "modules/gpu.h"
#include "modules/memory.h"
#include "modules/network.h"
#include "modules/process_table.h"
#include "modules/sensors.h"
#include "test_support.h"

#include <systemd/sd-event.h>
#include <unistd.h>

#include <gtest/gtest.h>

using namespace readout;
using namespace readout::test;

namespace {

SystemPaths pathsFor(const FakeTree& tree) {
    SystemPaths paths;
    paths.proc = tree.path("proc");
    paths.sys = tree.path("sys");
    paths.pciIds = tree.path("pci.ids");
    paths.pciIdsFallback = tree.path("missing");
    return paths;
}

SystemPaths fixturePaths() {
    SystemPaths paths;
    paths.proc = fixturePath("ryzen-9900x-arc-b580/proc");
    paths.sys = fixturePath("ryzen-9900x-arc-b580/sys");
    paths.pciIds = fixturePath("missing");
    paths.pciIdsFallback = fixturePath("missing");
    return paths;
}

SampleContext at(std::uint64_t seconds, Tier tier, const ProcessTable* processes = nullptr) {
    SampleContext context;
    context.nowMicroseconds = seconds * 1'000'000;
    context.tier = tier;
    context.processes = processes;
    return context;
}

sd_event* testEvent() {
    static sd_event* event = [] {
        sd_event* created = nullptr;
        sd_event_new(&created);
        return created;
    }();
    return event;
}

// A PCI display device with the attributes GPU code reads.
void addPciGpu(const FakeTree& tree, const std::string& slot, const char* vendor, const char* device, bool bootVga,
               const char* runtimeStatus, const std::string& card, const std::string& driver) {
    const std::string pci = "sys/devices/pci0000:00/" + slot;
    tree.write(pci + "/vendor", std::string(vendor) + "\n");
    tree.write(pci + "/device", std::string(device) + "\n");
    tree.write(pci + "/class", "0x030000\n");
    tree.write(pci + "/boot_vga", bootVga ? "1\n" : "0\n");
    tree.write(pci + "/power/runtime_status", std::string(runtimeStatus) + "\n");
    tree.directory("sys/bus/pci/drivers/" + driver);
    tree.link(pci + "/driver", tree.path("sys/bus/pci/drivers/" + driver));
    tree.link("sys/class/drm/" + card + "/device", tree.path(pci));
}

} // namespace

// ---- CPU ------------------------------------------------------------------------------------

TEST(CpuModule, ReadsTopologyModelAndTemperatureFromFixture) {
    CpuModule module(fixturePaths());
    const std::string info = encodeInfoToJson(module);
    EXPECT_EQ(jsonText(info, "model"), "AMD Ryzen 9 9900X 12-Core Processor");
    EXPECT_EQ(jsonNumber(info, "logicalCores"), 24);
    EXPECT_EQ(jsonNumber(info, "physicalCores"), 12);
    EXPECT_EQ(jsonText(info, "temperatureSource"), "k10temp Tctl");

    module.setTier(Tier::Detail);
    module.sample(at(1, Tier::Detail));
    const std::string json = encodeToJson(module, Tier::Detail);
    EXPECT_TRUE(jsonHasKey(json, "temperature"));
    EXPECT_GT(jsonNumber(json, "temperature").value_or(0), 20);
    EXPECT_GT(jsonNumber(json, "frequency").value_or(0), 100);
    EXPECT_TRUE(jsonHasKey(json, "cores"));
    EXPECT_GT(jsonNumber(json, "uptime").value_or(0), 0);
}

TEST(CpuModule, ComputesUsageBetweenSamples) {
    FakeTree tree;
    tree.write("proc/stat", "cpu  100 0 100 800 0 0 0 0\ncpu0 50 0 50 400 0 0 0 0\ncpu1 50 0 50 400 0 0 0 0\n");
    tree.directory("sys/devices/system/cpu/cpu0");
    tree.directory("sys/devices/system/cpu/cpu1");
    CpuModule module(pathsFor(tree));
    module.setTier(Tier::Panel);
    module.sample(at(1, Tier::Panel));
    tree.write("proc/stat", "cpu  160 0 120 820 0 0 0 0\ncpu0 110 0 60 400 0 0 0 0\ncpu1 50 0 60 420 0 0 0 0\n");
    module.sample(at(2, Tier::Panel));
    const std::string json = encodeToJson(module, Tier::Panel);
    EXPECT_NEAR(jsonNumber(json, "usage").value(), 80.0, 1e-9); // 80 of 100 ticks busy
    EXPECT_NEAR(jsonNumber(json, "user").value(), 60.0, 1e-9);
    EXPECT_FALSE(jsonHasKey(json, "cores")); // detail only, unless asked for
    EXPECT_FALSE(jsonHasKey(json, "temperature"));
    EncodeContext withCores;
    withCores.tier = Tier::Panel;
    withCores.cores = true;
    const std::string panelCores = encodeToJson(module, withCores);
    EXPECT_TRUE(jsonHasKey(panelCores, "cores")) << panelCores;
    EXPECT_FALSE(jsonHasKey(panelCores, "load1")) << panelCores;

    module.setTier(Tier::Detail);
    tree.write("proc/stat", "cpu  160 0 120 920 0 0 0 0\ncpu0 110 0 60 450 0 0 0 0\ncpu1 50 0 60 470 0 0 0 0\n");
    module.sample(at(3, Tier::Detail));
    const std::string detail = encodeToJson(module, Tier::Detail);
    EXPECT_NE(detail.find("\"cores\":[0,0]"), std::string::npos) << detail;
    EXPECT_EQ(module.history().front().buffer->size(), 2U);
}

TEST(CpuModule, DetectsIntelHybridCores) {
    FakeTree tree;
    tree.write("proc/stat", "cpu  0 0 0 10 0 0 0 0\ncpu0 0 0 0 10 0 0 0 0\ncpu1 0 0 0 10 0 0 0 0\n"
                            "cpu2 0 0 0 10 0 0 0 0\ncpu3 0 0 0 10 0 0 0 0\n");
    for (int cpu = 0; cpu < 4; ++cpu) {
        tree.write("sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/core_id", std::to_string(cpu));
        tree.write("sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/physical_package_id", "0");
    }
    tree.write("sys/devices/cpu_core/cpus", "0-1\n");
    tree.write("sys/devices/cpu_atom/cpus", "2-3\n");
    CpuModule module(pathsFor(tree));
    const std::string info = encodeInfoToJson(module);
    EXPECT_NE(info.find("\"hybrid\":true"), std::string::npos);
    EXPECT_NE(info.find("\"coreTypes\":[\"performance\",\"performance\",\"efficiency\",\"efficiency\"]"),
              std::string::npos)
        << info;

    module.setTier(Tier::Detail);
    module.sample(at(1, Tier::Detail));
    tree.write("proc/stat", "cpu  20 0 0 50 0 0 0 0\ncpu0 10 0 0 10 0 0 0 0\ncpu1 10 0 0 10 0 0 0 0\n"
                            "cpu2 0 0 0 20 0 0 0 0\ncpu3 0 0 0 20 0 0 0 0\n");
    module.sample(at(2, Tier::Detail));
    const std::string json = encodeToJson(module, Tier::Detail);
    EXPECT_NEAR(jsonNumber(json, "performanceUsage").value(), 100.0, 1e-9);
    EXPECT_NEAR(jsonNumber(json, "efficiencyUsage").value(), 0.0, 1e-9);
}

// ---- Memory ---------------------------------------------------------------------------------

TEST(MemoryModule, ReportsUsageAndDetailFromFixture) {
    MemoryModule module(fixturePaths());
    module.setTier(Tier::Detail);
    module.sample(at(1, Tier::Detail));
    const std::string json = encodeToJson(module, Tier::Detail);
    const double total = jsonNumber(json, "total").value();
    const double used = jsonNumber(json, "used").value();
    const double available = jsonNumber(json, "available").value();
    EXPECT_DOUBLE_EQ(used + available, total);
    EXPECT_NEAR(jsonNumber(json, "usage").value(), used * 100 / total, 1e-6);
    EXPECT_EQ(jsonText(json, "pressureLevel"), "normal");
    EXPECT_TRUE(jsonHasKey(json, "compressed"));
}

TEST(MemoryModule, PressureLevels) {
    Pressure pressure;
    EXPECT_STREQ(MemoryModule::pressureLevel(pressure), "normal");
    pressure.some.average10 = 15;
    EXPECT_STREQ(MemoryModule::pressureLevel(pressure), "warning");
    pressure.hasFull = true;
    pressure.full.average10 = 12;
    EXPECT_STREQ(MemoryModule::pressureLevel(pressure), "critical");
}

// ---- GPU ------------------------------------------------------------------------------------

TEST(GpuModule, ReadsAmdSysfsAndHwmon) {
    FakeTree tree;
    addPciGpu(tree, "0000:03:00.0", "0x1002", "0x744c", true, "active", "card0", "amdgpu");
    const std::string pci = "sys/devices/pci0000:00/0000:03:00.0";
    tree.write(pci + "/gpu_busy_percent", "37\n");
    tree.write(pci + "/mem_info_vram_used", "2147483648\n");
    tree.write(pci + "/mem_info_vram_total", "25753026560\n");
    tree.write(pci + "/hwmon/hwmon5/temp1_input", "52000\n");
    tree.write(pci + "/hwmon/hwmon5/temp1_label", "edge\n");
    tree.write(pci + "/hwmon/hwmon5/temp2_input", "61000\n");
    tree.write(pci + "/hwmon/hwmon5/temp2_label", "junction\n");
    tree.write(pci + "/hwmon/hwmon5/freq1_input", "2500000000\n");
    tree.write(pci + "/hwmon/hwmon5/freq2_input", "1250000000\n");
    tree.write(pci + "/hwmon/hwmon5/power1_average", "45000000\n");
    tree.write(pci + "/hwmon/hwmon5/fan1_input", "1100\n");
    tree.write(pci + "/hwmon/hwmon5/power1_cap", "303000000\n");
    tree.write(pci + "/pp_dpm_sclk", "0: 500Mhz\n1: 1800Mhz *\n2: 2500Mhz\n");
    tree.write("pci.ids", "1002  Advanced Micro Devices, Inc. [AMD/ATI]\n\t744c  Navi 31 [Radeon RX 7900 XT/7900 XTX]\n");

    GpuModule module(pathsFor(tree));
    const std::string info = encodeInfoToJson(module);
    EXPECT_EQ(jsonText(info, "name"), "AMD Radeon RX 7900 XT/7900 XTX");
    EXPECT_EQ(jsonText(info, "driver"), "amdgpu");

    module.setTier(Tier::Panel);
    module.sample(at(1, Tier::Panel));
    const std::string json = encodeToJson(module, Tier::Panel);
    EXPECT_EQ(jsonText(json, "state"), "active");
    EXPECT_EQ(jsonNumber(json, "usage"), 37);
    EXPECT_EQ(jsonNumber(json, "temperature"), 52);
    EXPECT_EQ(jsonNumber(json, "memoryUsed"), 2147483648.0);
    EXPECT_EQ(jsonNumber(json, "memoryTotal"), 25753026560.0);
    EXPECT_EQ(jsonNumber(json, "coreClock"), 2500);
    EXPECT_EQ(jsonNumber(json, "memoryClock"), 1250);
    EXPECT_EQ(jsonNumber(json, "power"), 45);
    EXPECT_EQ(jsonNumber(json, "fanRpm"), 1100);
    EXPECT_EQ(jsonNumber(json, "powerLimit"), 303);
    EXPECT_EQ(jsonNumber(json, "maxCoreClock"), 2500);
}

TEST(GpuModule, HighestDpmClock) {
    EXPECT_EQ(GpuModule::highestDpmMegahertz("0: 500Mhz\n1: 2500Mhz *\n2: 1200Mhz\n"), 2500);
    EXPECT_EQ(GpuModule::highestDpmMegahertz("S: 800Mhz\n"), 800);
    EXPECT_EQ(GpuModule::highestDpmMegahertz(""), -1);
    EXPECT_EQ(GpuModule::highestDpmMegahertz("garbage\n0: Mhz\n"), -1);
}

TEST(GpuModule, ComputesIntelXeUsageFromIdleResidency) {
    FakeTree tree;
    addPciGpu(tree, "0000:03:00.0", "0x8086", "0xe20b", true, "active", "card1", "xe");
    const std::string gt = "sys/devices/pci0000:00/0000:03:00.0/tile0/gt0";
    tree.write(gt + "/gtidle/name", "gt0-rc\n");
    tree.write(gt + "/gtidle/idle_residency_ms", "1000\n");
    tree.write(gt + "/freq0/act_freq", "0\n");
    tree.write(gt + "/freq0/rp0_freq", "2850\n");
    tree.write("sys/devices/pci0000:00/0000:03:00.0/tile0/gt1/gtidle/name", "gt1-mc\n");

    GpuModule module(pathsFor(tree));
    module.setTier(Tier::Panel);
    module.sample(at(10, Tier::Panel));
    tree.write(gt + "/gtidle/idle_residency_ms", "1750\n"); // idle 750 ms of 1000 ms
    tree.write(gt + "/freq0/act_freq", "2850\n");
    module.sample(at(11, Tier::Panel));
    const std::string json = encodeToJson(module, Tier::Panel);
    EXPECT_NEAR(jsonNumber(json, "usage").value(), 25.0, 1e-6);
    EXPECT_EQ(jsonNumber(json, "coreClock"), 2850);
    EXPECT_EQ(jsonNumber(json, "maxCoreClock"), 2850);
    EXPECT_EQ(jsonText(json, "name"), "Intel GPU"); // no pci.ids in this tree

    tree.write(gt + "/freq0/act_freq", "0\n"); // GT asleep: no clock reported
    module.sample(at(12, Tier::Panel));
    EXPECT_FALSE(jsonHasKey(encodeToJson(module, Tier::Panel), "coreClock"));
}

TEST(GpuModule, NeverReadsASuspendedGpu) {
    FakeTree tree;
    addPciGpu(tree, "0000:01:00.0", "0x1002", "0x73ff", true, "suspended", "card0", "amdgpu");
    tree.write("sys/devices/pci0000:00/0000:01:00.0/gpu_busy_percent", "99\n");
    GpuModule module(pathsFor(tree));
    module.setTier(Tier::Detail);
    module.sample(at(1, Tier::Detail));
    const std::string json = encodeToJson(module, Tier::Detail);
    EXPECT_EQ(jsonText(json, "state"), "suspended");
    EXPECT_FALSE(jsonHasKey(json, "usage"));
}

TEST(GpuModule, SecondaryGpuIsOnlyReadInDetailTier) {
    FakeTree tree;
    addPciGpu(tree, "0000:00:02.0", "0x8086", "0x46a6", true, "active", "card0", "i915");
    addPciGpu(tree, "0000:01:00.0", "0x1002", "0x73ff", false, "active", "card1", "amdgpu");
    tree.write("sys/devices/pci0000:00/0000:01:00.0/gpu_busy_percent", "64\n");
    GpuModule module(pathsFor(tree));
    const std::string info = encodeInfoToJson(module);
    EXPECT_NE(info.find("\"id\":\"0000:01:00.0\",\"name\":\"AMD GPU\",\"driver\":\"amdgpu\",\"vendor\":\"AMD\","
                        "\"secondary\":true"),
              std::string::npos)
        << info;

    module.setTier(Tier::Panel);
    module.sample(at(1, Tier::Panel));
    const std::string panel = encodeToJson(module, Tier::Panel);
    const std::size_t amd = panel.find("0000:01:00.0");
    ASSERT_NE(amd, std::string::npos);
    EXPECT_EQ(jsonText(panel, "state", amd), "idle");
    EXPECT_FALSE(jsonNumber(panel, "usage", amd).has_value());

    module.setTier(Tier::Detail);
    module.sample(at(2, Tier::Detail));
    const std::string detail = encodeToJson(module, Tier::Detail);
    const std::size_t amdDetail = detail.find("0000:01:00.0");
    EXPECT_EQ(jsonText(detail, "state", amdDetail), "active");
    EXPECT_EQ(jsonNumber(detail, "usage", amdDetail), 64);
}

TEST(GpuModule, FriendlyNames) {
    EXPECT_EQ(GpuModule::friendlyName("Intel Corporation", "Battlemage G21 [Arc B580]", 0x8086), "Intel Arc B580");
    EXPECT_EQ(GpuModule::friendlyName("NVIDIA Corporation", "AD104 [GeForce RTX 4070]", 0x10de),
              "NVIDIA GeForce RTX 4070");
    EXPECT_EQ(GpuModule::friendlyName("Some Vendor", "Plain Device", 0x1234), "Plain Device");
    EXPECT_EQ(GpuModule::friendlyName("", "", 0x1234), "GPU");
}

// ---- Sensors --------------------------------------------------------------------------------

namespace {

void addHwmon(const FakeTree& tree, const std::string& hwmon, const std::string& name, const std::string& device) {
    tree.write("sys/class/hwmon/" + hwmon + "/name", name + "\n");
    if (!device.empty()) {
        tree.link("sys/class/hwmon/" + hwmon + "/device", tree.path(device));
    }
}

} // namespace

TEST(SensorsModule, PanelReadsOnlySummaryAndSelectedSensors) {
    FakeTree tree;
    tree.write("sys/devices/pci0000:00/0000:00:18.3/power/runtime_status", "unsupported\n");
    addHwmon(tree, "hwmon0", "k10temp", "sys/devices/pci0000:00/0000:00:18.3");
    tree.write("sys/class/hwmon/hwmon0/temp1_input", "50625\n");
    tree.write("sys/class/hwmon/hwmon0/temp1_label", "Tctl\n");
    tree.write("sys/class/hwmon/hwmon0/temp3_input", "40250\n");
    tree.write("sys/class/hwmon/hwmon0/temp3_label", "Tccd1\n");
    addHwmon(tree, "hwmon1", "nvme", "");
    tree.write("sys/class/hwmon/hwmon1/temp1_input", "38850\n");
    tree.write("sys/class/hwmon/hwmon1/temp1_label", "Composite\n");
    tree.write("sys/class/hwmon/hwmon1/fan1_input", "1200\n");
    tree.write("sys/class/hwmon/hwmon1/in0_input", "1200\n");

    SensorsModule module(pathsFor(tree));
    const std::string info = encodeInfoToJson(module);
    EXPECT_NE(info.find("k10temp@0000:00:18.3/temp1"), std::string::npos) << info;
    EXPECT_NE(info.find("\"id\":\"nvme/temp1\""), std::string::npos) << info;

    module.setTier(Tier::Panel);
    const std::vector<std::string> selected = {"nvme/fan1"};
    module.setSelectedSensors(selected);
    module.sample(at(1, Tier::Panel));
    const std::string panel = encodeToJson(module, Tier::Panel, selected);
    EXPECT_NEAR(jsonNumber(panel, "cpuTemperature").value(), 50.625, 1e-9);
    EXPECT_FALSE(jsonHasKey(panel, "gpuTemperature"));
    EXPECT_NE(panel.find("\"id\":\"nvme/fan1\""), std::string::npos);
    EXPECT_EQ(panel.find("\"id\":\"nvme/temp1\""), std::string::npos); // not selected: not sent
    EXPECT_EQ(jsonNumber(panel, "value", panel.find("nvme/fan1")), 1200);

    module.setTier(Tier::Detail);
    module.sample(at(2, Tier::Detail));
    const std::string detail = encodeToJson(module, Tier::Detail);
    const std::size_t voltage = detail.find("\"id\":\"nvme/in0\"");
    ASSERT_NE(voltage, std::string::npos);
    EXPECT_NEAR(jsonNumber(detail, "value", voltage).value(), 1.2, 1e-9);
    EXPECT_EQ(jsonText(detail, "group", detail.find("nvme/temp1")), "Storage");
    EXPECT_NEAR(jsonNumber(detail, "maxTemperature").value(), 50.625, 1e-9);
}

TEST(SensorsModule, SkipsSuspendedDevicesAndComputesPowerFromEnergy) {
    FakeTree tree;
    addPciGpu(tree, "0000:03:00.0", "0x8086", "0xe20b", true, "suspended", "card1", "xe");
    addHwmon(tree, "hwmon2", "xe", "sys/devices/pci0000:00/0000:03:00.0");
    tree.write("sys/class/hwmon/hwmon2/temp2_input", "45000\n");
    tree.write("sys/class/hwmon/hwmon2/temp2_label", "pkg\n");
    tree.write("sys/class/hwmon/hwmon2/energy1_input", "1000000\n");
    tree.write("sys/class/hwmon/hwmon2/energy1_label", "card\n");

    SensorsModule module(pathsFor(tree));
    module.setTier(Tier::Detail);
    module.sample(at(1, Tier::Detail));
    std::string json = encodeToJson(module, Tier::Detail);
    EXPECT_NE(json.find("\"sleeping\":true"), std::string::npos) << json;
    EXPECT_FALSE(jsonHasKey(json, "gpuTemperature"));

    tree.write("sys/devices/pci0000:00/0000:03:00.0/power/runtime_status", "active\n");
    module.sample(at(2, Tier::Detail));
    tree.write("sys/class/hwmon/hwmon2/energy1_input", "31000000\n"); // 30 J in 1 s
    module.sample(at(3, Tier::Detail));
    json = encodeToJson(module, Tier::Detail);
    EXPECT_EQ(jsonNumber(json, "gpuTemperature"), 45);
    const std::size_t energy = json.find("xe@0000:03:00.0/energy1");
    ASSERT_NE(energy, std::string::npos);
    EXPECT_NEAR(jsonNumber(json, "value", energy).value(), 30.0, 1e-9);
    EXPECT_EQ(jsonText(json, "kind", energy), "power");
}

TEST(SensorsModule, Groups) {
    EXPECT_STREQ(SensorsModule::groupForChip("k10temp"), "CPU");
    EXPECT_STREQ(SensorsModule::groupForChip("amdgpu"), "GPU");
    EXPECT_STREQ(SensorsModule::groupForChip("nvme"), "Storage");
    EXPECT_STREQ(SensorsModule::groupForChip("spd5118"), "Memory");
    EXPECT_STREQ(SensorsModule::groupForChip("ath12k_hwmon"), "Wireless");
    EXPECT_STREQ(SensorsModule::groupForChip("r8169_0_e00:00"), "Network");
    EXPECT_STREQ(SensorsModule::groupForChip("nct6798"), "System");
}

// ---- Disk -----------------------------------------------------------------------------------

TEST(DiskModule, MapsMountsToDisksAndComputesRates) {
    FakeTree tree;
    tree.directory("sys/devices/virtual/block/vdz/device");
    tree.link("sys/block/vdz", tree.path("sys/devices/virtual/block/vdz"));
    tree.write("sys/block/vdz/dev", "252:0\n");
    tree.write("sys/block/vdz/size", "2000000\n");
    tree.write("sys/block/vdz/removable", "0\n");
    tree.write("sys/block/vdz/queue/rotational", "1\n");
    tree.write("sys/block/vdz/device/model", "Fake Disk  \n");
    tree.write("sys/devices/virtual/block/vdz/vdz2/partition", "2\n");
    tree.link("sys/class/block/vdz2", tree.path("sys/devices/virtual/block/vdz/vdz2"));
    tree.write("proc/self/mountinfo",
               "1 0 252:2 / / rw - ext4 /dev/vdz2 rw\n"
               "2 1 0:5 / /proc rw - proc proc rw\n"
               "3 1 252:2 /home " + tree.root() + " rw - ext4 /dev/vdz2 rw\n");
    tree.write("proc/diskstats", " 252 0 vdz 10 0 2000 0 10 0 4000 0 0 100 0\n");

    SystemBus systemBus(testEvent());
    systemBus.disable();
    DiskModule module(pathsFor(tree), testEvent(), systemBus);
    module.setTier(Tier::Panel);
    module.sample(at(1, Tier::Panel));
    tree.write("proc/diskstats", " 252 0 vdz 20 0 4048 0 20 0 6048 0 0 600 0\n"); // 2048 sectors each way in 2 s
    module.sample(at(3, Tier::Panel));
    const std::string panel = encodeToJson(module, Tier::Panel);
    EXPECT_NEAR(jsonNumber(panel, "read").value(), 2048.0 * 512 / 2, 1e-6);
    EXPECT_NEAR(jsonNumber(panel, "write").value(), 2048.0 * 512 / 2, 1e-6);
    EXPECT_EQ(jsonText(panel, "mountPoint"), "/");
    EXPECT_GT(jsonNumber(panel, "total").value_or(0), 0);

    // Totals plus one read and one write series per drive, kept in every tier.
    const auto series = module.history();
    ASSERT_EQ(series.size(), 4U);
    EXPECT_STREQ(series[2].name, "vdz/read");
    EXPECT_STREQ(series[3].name, "vdz/write");
    EXPECT_EQ(series[2].buffer->size(), 2U);

    const std::string info = encodeInfoToJson(module);
    EXPECT_EQ(jsonText(info, "model"), "Fake Disk");
    EXPECT_NE(info.find("\"rotational\":true"), std::string::npos);
}

TEST(DiskModule, RealFilesystems) {
    EXPECT_TRUE(DiskModule::isRealFilesystem("btrfs", "/dev/nvme0n1p3"));
    EXPECT_TRUE(DiskModule::isRealFilesystem("zfs", "pool/root"));
    EXPECT_FALSE(DiskModule::isRealFilesystem("tmpfs", "tmpfs"));
    EXPECT_FALSE(DiskModule::isRealFilesystem("squashfs", "/dev/loop0"));
    EXPECT_FALSE(DiskModule::isRealFilesystem("overlay", "overlay"));
    EXPECT_FALSE(DiskModule::isRealFilesystem("nfs4", "server:/export"));
    EXPECT_FALSE(DiskModule::isRealFilesystem("ext4", "none"));
}

// ---- Network --------------------------------------------------------------------------------

TEST(NetworkModule, PicksDefaultRouteInterfaceAndComputesRates) {
    FakeTree tree;
    tree.write("proc/net/dev", "Inter-|\n face |\n    lo: 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
                               "  eth9: 1000 1 0 0 0 0 0 0 500 1 0 0 0 0 0 0\n"
                               "  veth1: 99 1 0 0 0 0 0 0 99 1 0 0 0 0 0 0\n");
    tree.write("proc/net/route", "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n"
                                 "eth9\t00000000\t0100A8C0\t0003\t0\t0\t100\t00000000\t0\t0\t0\n"
                                 "eth9\t0000A8C0\t00000000\t0001\t0\t0\t100\t00FFFFFF\t0\t0\t0\n");
    tree.directory("sys/class/net/eth9/device");
    tree.write("sys/class/net/eth9/operstate", "up\n");
    tree.write("sys/class/net/veth1/operstate", "down\n");
    tree.directory("sys/class/net/docker0/bridge");
    tree.write("sys/class/net/docker0/operstate", "up\n");

    SystemBus systemBus(testEvent());
    systemBus.disable();
    NetworkModule module(pathsFor(tree), testEvent(), systemBus);
    module.setTier(Tier::Panel);
    module.sample(at(1, Tier::Panel));
    tree.write("proc/net/dev", "Inter-|\n face |\n    lo: 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
                               "  eth9: 3000 1 0 0 0 0 0 0 1500 1 0 0 0 0 0 0\n"
                               "  veth1: 99 1 0 0 0 0 0 0 99 1 0 0 0 0 0 0\n");
    module.sample(at(3, Tier::Panel));
    const std::string json = encodeToJson(module, Tier::Panel);
    EXPECT_EQ(jsonText(json, "interface"), "eth9");
    EXPECT_NEAR(jsonNumber(json, "download").value(), 1000.0, 1e-9);
    EXPECT_NEAR(jsonNumber(json, "upload").value(), 500.0, 1e-9);
    EXPECT_EQ(jsonNumber(json, "totalDownloaded"), 3000);
    EXPECT_EQ(jsonText(json, "connectionType"), "ethernet");
    EXPECT_NE(json.find("\"connected\":true"), std::string::npos);

    module.setTier(Tier::Detail);
    module.sample(at(4, Tier::Detail));
    const std::string detail = encodeToJson(module, Tier::Detail);
    EXPECT_NE(detail.find("\"name\":\"eth9\""), std::string::npos);
    EXPECT_EQ(detail.find("\"name\":\"veth1\""), std::string::npos); // down virtual interface hidden
    EXPECT_EQ(detail.find("\"name\":\"docker0\""), std::string::npos); // container bridge hidden
    EXPECT_EQ(detail.find("\"name\":\"lo\""), std::string::npos);
}

TEST(NetworkModule, CounterResetGivesZeroRate) {
    FakeTree tree;
    tree.write("proc/net/dev", "  eth9: 5000 1 0 0 0 0 0 0 5000 1 0 0 0 0 0 0\n");
    tree.directory("sys/class/net/eth9/device");
    tree.write("sys/class/net/eth9/operstate", "up\n");
    SystemBus systemBus(testEvent());
    systemBus.disable();
    NetworkModule module(pathsFor(tree), testEvent(), systemBus);
    module.setTier(Tier::Panel);
    module.sample(at(1, Tier::Panel));
    tree.write("proc/net/dev", "  eth9: 10 1 0 0 0 0 0 0 10 1 0 0 0 0 0 0\n");
    module.sample(at(2, Tier::Panel));
    EXPECT_EQ(jsonNumber(encodeToJson(module, Tier::Panel), "download"), 0);
}

TEST(NetworkModule, SecurityNames) {
    EXPECT_STREQ(NetworkModule::securityName(0, 0, 0x400), "WPA3");
    EXPECT_STREQ(NetworkModule::securityName(1, 0, 0x100), "WPA2");
    EXPECT_STREQ(NetworkModule::securityName(1, 0, 0x200), "WPA2 Enterprise");
    EXPECT_STREQ(NetworkModule::securityName(1, 0x100, 0), "WPA");
    EXPECT_STREQ(NetworkModule::securityName(1, 0, 0), "WEP");
    EXPECT_STREQ(NetworkModule::securityName(0, 0, 0), "Open");
}

// ---- Processes ------------------------------------------------------------------------------

TEST(ProcessTable, FindsThisProcessAndMeasuresCpu) {
    ProcessTable table(SystemPaths{});
    table.refresh(1'000'000, true);
    volatile double sink = 0;
    for (int index = 0; index < 20'000'000; ++index) {
        sink = sink + index * 0.5;
    }
    table.refresh(2'000'000, true);
    const int self = static_cast<int>(::getpid());
    bool found = false;
    for (const ProcessEntry& entry : table.entries()) {
        if (entry.pid == self) {
            found = true;
            EXPECT_GT(entry.residentBytes, 0U);
            EXPECT_GT(entry.cpuPercent, 0.0);
            EXPECT_TRUE(entry.ioKnown);
        }
    }
    EXPECT_TRUE(found);
    std::vector<std::uint32_t> top;
    table.top(ProcessTable::SortKey::Memory, 3, top);
    ASSERT_EQ(top.size(), 3U);
    EXPECT_GE(table.entries()[top[0]].residentBytes, table.entries()[top[1]].residentBytes);
    EXPECT_NE(table.displayName(top[0])[0], '\0');
}

// ---- Battery --------------------------------------------------------------------------------

TEST(BatteryModule, AppliesUPowerProperties) {
    sd_bus* bus = testBus();
    ASSERT_NE(bus, nullptr);
    sd_bus_message* message = nullptr;
    ASSERT_GE(sd_bus_message_new_signal(bus, &message, "/test", "test.Interface", "Test"), 0);
    ASSERT_GE(sd_bus_message_append(message, "a{sv}", 6, "Percentage", "d", 76.5, "State", "u", 2U, "Model", "s",
                                    "5B10W51867", "ChargeCycles", "i", 312, "PowerSupply", "b", 1, "Type", "u", 2U),
              0);
    ASSERT_GE(sd_bus_message_seal(message, 1, 0), 0);
    ASSERT_GE(sd_bus_message_rewind(message, 1), 0);
    BatteryModule::Device device;
    readProperties(message, [&](std::string_view key, VariantValue& value) {
        BatteryModule::applyProperty(device, key, value);
    });
    sd_bus_message_unref(message);
    EXPECT_DOUBLE_EQ(device.percentage, 76.5);
    EXPECT_EQ(device.state, 2U);
    EXPECT_EQ(device.model, "5B10W51867");
    EXPECT_EQ(device.cycles, 312);
    EXPECT_TRUE(device.powerSupply);
    EXPECT_EQ(device.type, 2U);
    EXPECT_STREQ(BatteryModule::stateName(2), "discharging");
    EXPECT_STREQ(BatteryModule::kindName(5), "mouse");
    EXPECT_STREQ(BatteryModule::kindName(999), "unknown");
}
