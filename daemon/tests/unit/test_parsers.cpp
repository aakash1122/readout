#include "parsers/kernel_text.h"
#include "test_support.h"

#include <gtest/gtest.h>

#include <map>
#include <vector>

using namespace readout;
using readout::test::readFixture;

namespace {
constexpr const char* machine = "ryzen-9900x-arc-b580/";
}

TEST(ProcStat, ParsesAggregateAndCores) {
    const std::string text = "cpu  100 5 50 1000 10 1 2 3 0 0\n"
                             "cpu0 60 2 30 400 5 1 1 2 0 0\n"
                             "cpu2 40 3 20 600 5 0 1 1 0 0\n"
                             "intr 1234 0 0\n"
                             "ctxt 99\n";
    std::map<int, CpuTimes> lines;
    parseProcStat(text, [&](int index, const CpuTimes& times) { lines[index] = times; });
    ASSERT_EQ(lines.size(), 3U);
    EXPECT_EQ(lines[-1].user, 100U);
    EXPECT_EQ(lines[-1].steal, 3U);
    EXPECT_EQ(lines[-1].total(), 100U + 5 + 50 + 1000 + 10 + 1 + 2 + 3);
    EXPECT_EQ(lines[2].idle, 600U);
    EXPECT_EQ(lines.count(1), 0U); // offline CPU: gap in numbering
}

TEST(ProcStat, ParsesRealFixture) {
    int cores = 0;
    bool aggregate = false;
    parseProcStat(readFixture(std::string(machine) + "proc/stat"), [&](int index, const CpuTimes& times) {
        if (index < 0) {
            aggregate = true;
            EXPECT_GT(times.total(), 0U);
        } else {
            ++cores;
        }
    });
    EXPECT_TRUE(aggregate);
    EXPECT_EQ(cores, 24);
}

TEST(ProcStat, ComputesUsageDelta) {
    CpuTimes before;
    before.user = 100;
    before.system = 100;
    before.idle = 800;
    CpuTimes after = before;
    after.user += 30;
    after.system += 20;
    after.idle += 50;
    after.ioWait += 0;
    const CpuUsage usage = computeCpuUsage(before, after);
    EXPECT_DOUBLE_EQ(usage.total, 50.0);
    EXPECT_DOUBLE_EQ(usage.user, 30.0);
    EXPECT_DOUBLE_EQ(usage.system, 20.0);
}

TEST(ProcStat, UsageIsZeroWhenCountersDoNotAdvanceOrGoBack) {
    CpuTimes sample;
    sample.user = 10;
    sample.idle = 10;
    EXPECT_DOUBLE_EQ(computeCpuUsage(sample, sample).total, 0.0);
    CpuTimes earlier = sample;
    earlier.idle = 5;
    EXPECT_DOUBLE_EQ(computeCpuUsage(sample, earlier).total, 0.0);
}

TEST(ProcStat, IoWaitCountsAsIdle) {
    CpuTimes before;
    CpuTimes after;
    after.ioWait = 50;
    after.user = 50;
    const CpuUsage usage = computeCpuUsage(before, after);
    EXPECT_DOUBLE_EQ(usage.total, 50.0);
    EXPECT_DOUBLE_EQ(usage.ioWait, 50.0);
}

TEST(Meminfo, ParsesKilobytesToBytes) {
    const MemoryInfo info = parseMeminfo("MemTotal:       16000 kB\n"
                                         "MemFree:         2000 kB\n"
                                         "MemAvailable:    9000 kB\n"
                                         "Buffers:          100 kB\n"
                                         "Cached:          5000 kB\n"
                                         "SwapTotal:       8000 kB\n"
                                         "SwapFree:        7000 kB\n"
                                         "Shmem:            300 kB\n"
                                         "SReclaimable:     400 kB\n"
                                         "AnonPages:       4000 kB\n"
                                         "HugePages_Total:       0\n");
    EXPECT_EQ(info.total, 16000U * 1024);
    EXPECT_EQ(info.available, 9000U * 1024);
    EXPECT_EQ(info.swapFree, 7000U * 1024);
    EXPECT_EQ(info.anonymous, 4000U * 1024);
    EXPECT_TRUE(info.hasAvailable);
}

TEST(Meminfo, ApproximatesAvailableOnOldKernels) {
    const MemoryInfo info = parseMeminfo("MemTotal: 1000 kB\nMemFree: 100 kB\nBuffers: 10 kB\nCached: 200 kB\n");
    EXPECT_FALSE(info.hasAvailable);
    EXPECT_EQ(info.available, 310U * 1024);
}

TEST(Meminfo, ParsesRealFixture) {
    const MemoryInfo info = parseMeminfo(readFixture(std::string(machine) + "proc/meminfo"));
    EXPECT_GT(info.total, 1024ULL * 1024 * 1024);
    EXPECT_LE(info.available, info.total);
    EXPECT_TRUE(info.hasAvailable);
}

TEST(LoadAverage, Parses) {
    const auto load = parseLoadAverage("0.51 0.67 0.68 2/2833 401234\n");
    ASSERT_TRUE(load.has_value());
    EXPECT_DOUBLE_EQ(load->oneMinute, 0.51);
    EXPECT_DOUBLE_EQ(load->fifteenMinutes, 0.68);
    EXPECT_EQ(load->runnable, 2U);
    EXPECT_EQ(load->total, 2833U);
    EXPECT_FALSE(parseLoadAverage("garbage").has_value());
}

TEST(Uptime, Parses) {
    EXPECT_DOUBLE_EQ(parseUptimeSeconds("9499.05 220000.10\n").value(), 9499.05);
    EXPECT_FALSE(parseUptimeSeconds("").has_value());
}

TEST(Pressure, ParsesSomeAndFull) {
    const auto pressure = parsePressure("some avg10=12.50 avg60=3.00 avg300=1.00 total=2239679\n"
                                        "full avg10=4.25 avg60=0.00 avg300=0.00 total=2204220\n");
    ASSERT_TRUE(pressure.has_value());
    EXPECT_DOUBLE_EQ(pressure->some.average10, 12.5);
    EXPECT_EQ(pressure->some.totalMicroseconds, 2239679U);
    EXPECT_TRUE(pressure->hasFull);
    EXPECT_DOUBLE_EQ(pressure->full.average10, 4.25);
}

TEST(Pressure, CpuFileWithoutFullLine) {
    const auto pressure = parsePressure("some avg10=1.00 avg60=0.00 avg300=0.00 total=1\n");
    ASSERT_TRUE(pressure.has_value());
    EXPECT_FALSE(pressure->hasFull);
    EXPECT_FALSE(parsePressure("").has_value());
}

TEST(NetDev, ParsesInterfaces) {
    const std::string text =
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls "
        "carrier compressed\n"
        "    lo: 1000 10 0 0 0 0 0 0 1000 10 0 0 0 0 0 0\n"
        "wlp15s0:5644525131 4000000 1 2 0 0 0 0 151005886 900000 3 4 0 0 0 0\n";
    std::map<std::string, InterfaceCounters> interfaces;
    parseNetDev(text, [&](std::string_view name, const InterfaceCounters& counters) {
        interfaces[std::string(name)] = counters;
    });
    ASSERT_EQ(interfaces.size(), 2U);
    EXPECT_EQ(interfaces["wlp15s0"].receivedBytes, 5644525131U);
    EXPECT_EQ(interfaces["wlp15s0"].sentBytes, 151005886U);
    EXPECT_EQ(interfaces["wlp15s0"].receiveErrors, 1U);
    EXPECT_EQ(interfaces["wlp15s0"].sendDrops, 4U);
}

TEST(NetDev, ParsesRealFixture) {
    int count = 0;
    parseNetDev(readFixture(std::string(machine) + "proc/net/dev"),
                [&](std::string_view /*name*/, const InterfaceCounters& /*counters*/) { ++count; });
    EXPECT_GE(count, 2);
}

TEST(DiskStats, ParsesCounters) {
    const std::string text = " 259       0 nvme0n1 1000 5 21083176 300 2000 10 30198938 400 0 500 700 0 0 0 0 0 0\n"
                             " 259       1 nvme0n1p1 10 0 100 1 0 0 0 0 0 2 3 0 0 0 0\n"
                             "   7       0 loop0 short line\n";
    std::vector<std::pair<std::string, DiskCounters>> disks;
    parseDiskStats(text, [&](std::string_view name, const DiskCounters& counters) {
        disks.emplace_back(std::string(name), counters);
    });
    ASSERT_EQ(disks.size(), 2U);
    EXPECT_EQ(disks[0].first, "nvme0n1");
    EXPECT_EQ(disks[0].second.major, 259U);
    EXPECT_EQ(disks[0].second.sectorsRead, 21083176U);
    EXPECT_EQ(disks[0].second.sectorsWritten, 30198938U);
    EXPECT_EQ(disks[0].second.ioMilliseconds, 500U);
}

TEST(MountInfo, ParsesFieldsAndOptionalTags) {
    const std::string text =
        "43 1 0:35 /root / rw,relatime shared:1 - btrfs /dev/nvme0n1p3 rw,compress=zstd:1\n"
        "90 43 259:2 / /boot rw,relatime shared:50 master:3 - ext4 /dev/nvme0n1p2 rw\n"
        "91 43 8:17 / /run/media/user/My\\040Disk rw - exfat /dev/sdb1 rw\n"
        "bad line\n";
    std::vector<MountEntry> mounts;
    parseMountInfo(text, [&](const MountEntry& entry) { mounts.push_back(entry); });
    ASSERT_EQ(mounts.size(), 3U);
    EXPECT_EQ(mounts[0].fsType, "btrfs");
    EXPECT_EQ(mounts[0].source, "/dev/nvme0n1p3");
    EXPECT_EQ(mounts[0].major, 0U);
    EXPECT_EQ(mounts[1].mountPoint, "/boot");
    EXPECT_EQ(mounts[1].fsType, "ext4");
    EXPECT_EQ(unescapeMountField(mounts[2].mountPoint), "/run/media/user/My Disk");
}

TEST(MountInfo, UnescapeLeavesInvalidEscapes) {
    EXPECT_EQ(unescapeMountField("a\\134b"), "a\\b");
    EXPECT_EQ(unescapeMountField("tab\\011"), "tab\t");
    EXPECT_EQ(unescapeMountField("bad\\9"), "bad\\9");
    EXPECT_EQ(unescapeMountField("end\\"), "end\\");
}

TEST(ProcessStat, ParsesCommandWithSpacesAndParentheses) {
    const auto stat = parseProcessStat("1234 (Web (Content) 1) S 1 1234 1234 0 -1 4194560 100 0 0 0 "
                                       "250 50 0 0 20 0 30 0 987654 1000000 4096 18446744073709551615");
    ASSERT_TRUE(stat.has_value());
    EXPECT_EQ(stat->command, "Web (Content) 1");
    EXPECT_EQ(stat->state, 'S');
    EXPECT_EQ(stat->userTicks, 250U);
    EXPECT_EQ(stat->systemTicks, 50U);
    EXPECT_EQ(stat->startTimeTicks, 987654U);
    EXPECT_EQ(stat->residentPages, 4096);
}

TEST(ProcessStat, RejectsTruncatedLines) {
    EXPECT_FALSE(parseProcessStat("1 (init) S 0 1").has_value());
    EXPECT_FALSE(parseProcessStat("no parentheses").has_value());
}

TEST(ProcessIo, ParsesReadAndWriteBytes) {
    const auto io = parseProcessIo("rchar: 1\nwchar: 2\nsyscr: 3\nsyscw: 4\nread_bytes: 4096\n"
                                   "write_bytes: 8192\ncancelled_write_bytes: 0\n");
    ASSERT_TRUE(io.has_value());
    EXPECT_EQ(io->readBytes, 4096U);
    EXPECT_EQ(io->writeBytes, 8192U);
    EXPECT_FALSE(parseProcessIo("rchar: 1\n").has_value());
}

TEST(CpuInfo, FindsModelOnX86AndArm) {
    EXPECT_EQ(parseCpuModel(readFixture(std::string(machine) + "proc/cpuinfo")), "AMD Ryzen 9 9900X 12-Core Processor");
    EXPECT_EQ(parseCpuModel("processor\t: 0\nBogoMIPS\t: 108.00\nHardware\t: BCM2835\nModel\t: Raspberry Pi 4\n"),
              "BCM2835");
    EXPECT_EQ(parseCpuModel(""), "");
}

TEST(CpuList, ParsesRangesAndRejectsBadInput) {
    std::vector<int> cpus;
    EXPECT_TRUE(parseCpuList("0-3,8,10-11\n", [&](int cpu) { cpus.push_back(cpu); }));
    EXPECT_EQ(cpus, (std::vector<int>{0, 1, 2, 3, 8, 10, 11}));
    EXPECT_FALSE(parseCpuList("5-2", [](int) {}));
    EXPECT_FALSE(parseCpuList("x", [](int) {}));
}

TEST(Zram, ParsesMmStat) {
    const auto stat = parseZramMmStat("4096 20480 65536 0 65536 0 0 0 0\n");
    ASSERT_TRUE(stat.has_value());
    EXPECT_EQ(stat->originalBytes, 4096U);
    EXPECT_EQ(stat->compressedBytes, 20480U);
    EXPECT_EQ(stat->memoryUsedBytes, 65536U);
}

TEST(PciIds, FindsVendorAndDevice) {
    const std::string database = "# comment\n"
                                 "1002  Advanced Micro Devices, Inc. [AMD/ATI]\n"
                                 "\t744c  Navi 31 [Radeon RX 7900 XT/7900 XTX/7900 GRE/7900M]\n"
                                 "8086  Intel Corporation\n"
                                 "\te20b  Battlemage G21 [Arc B580]\n"
                                 "\t\t8086 1100  Arc B580 subsystem\n"
                                 "10de  NVIDIA Corporation\n"
                                 "C 00  Unclassified device\n";
    const PciName intel = lookupPciName(database, 0x8086, 0xe20b);
    EXPECT_EQ(intel.vendor, "Intel Corporation");
    EXPECT_EQ(intel.device, "Battlemage G21 [Arc B580]");
    const PciName unknown = lookupPciName(database, 0x10de, 0x1234);
    EXPECT_EQ(unknown.vendor, "NVIDIA Corporation");
    EXPECT_EQ(unknown.device, "");
}

TEST(KeyValue, FindsKey) {
    EXPECT_EQ(findKeyValue("drm-driver:\txe\ndrm-client-id:\t7\n", "drm-client-id", ':').value(), "7");
    EXPECT_FALSE(findKeyValue("a: 1\n", "b", ':').has_value());
}
