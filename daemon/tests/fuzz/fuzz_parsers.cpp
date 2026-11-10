// libFuzzer target covering every text parser. The first byte selects the parser.
//
//   cmake -DREADOUT_FUZZ=ON -DCMAKE_CXX_COMPILER=clang++ ...
//   build/daemon/tests/fuzz_parsers -max_total_time=60 daemon/tests/fuzz/corpus

#include "common/text.h"
#include "parsers/kernel_text.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

using namespace readout;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) {
        return 0;
    }
    const std::uint8_t selector = data[0];
    const std::string_view text(reinterpret_cast<const char*>(data + 1), size - 1);
    volatile std::uint64_t sink = 0;
    switch (selector % 16) {
        case 0:
            parseProcStat(text, [&](int index, const CpuTimes& times) { sink = sink + times.total() + index; });
            break;
        case 1:
            sink = parseMeminfo(text).total;
            break;
        case 2:
            sink = parseLoadAverage(text).has_value();
            break;
        case 3:
            sink = parsePressure(text).has_value();
            break;
        case 4:
            parseNetDev(text, [&](std::string_view name, const InterfaceCounters& counters) {
                sink = sink + name.size() + counters.receivedBytes;
            });
            break;
        case 5:
            parseDiskStats(text, [&](std::string_view name, const DiskCounters& counters) {
                sink = sink + name.size() + counters.sectorsRead;
            });
            break;
        case 6:
            parseMountInfo(text, [&](const MountEntry& entry) {
                sink = sink + unescapeMountField(entry.mountPoint).size() + entry.fsType.size();
            });
            break;
        case 7: {
            const auto stat = parseProcessStat(text);
            sink = stat ? stat->command.size() + stat->userTicks : 0;
            break;
        }
        case 8:
            sink = parseProcessIo(text).has_value();
            break;
        case 9:
            sink = parseCpuModel(text).size();
            break;
        case 10:
            parseCpuList(text, [&](int cpu) { sink = sink + static_cast<std::uint64_t>(cpu); });
            break;
        case 11:
            sink = parseZramMmStat(text).has_value();
            break;
        case 12: {
            const PciName name = lookupPciName(text, 0x8086, 0xe20b);
            sink = name.vendor.size() + name.device.size();
            break;
        }
        case 13: {
            std::array<char, 64> output{};
            copySanitizedUtf8(text, output.data(), output.size());
            sink = output[0];
            break;
        }
        case 14:
            sink = parseUptimeSeconds(text).has_value();
            break;
        default:
            sink = findKeyValue(text, "drm-driver", ':').has_value();
            break;
    }
    (void)sink;
    return 0;
}
