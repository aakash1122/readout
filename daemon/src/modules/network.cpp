#include "modules/network.h"

#include "common/log.h"
#include "common/text.h"
#include "io/sysfs.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>

namespace readout {

namespace {

constexpr const char* networkManagerService = "org.freedesktop.NetworkManager";
constexpr const char* networkManagerPath = "/org/freedesktop/NetworkManager";

constexpr std::uint32_t nmDeviceTypeWifi = 2;

const char* connectivityName(std::uint32_t state) {
    switch (state) {
        case 1:
            return "none";
        case 2:
            return "portal";
        case 3:
            return "limited";
        case 4:
            return "full";
        default:
            return "unknown";
    }
}

std::string connectionTypeFromNetworkManager(std::string_view type) {
    if (type == "802-3-ethernet") {
        return "ethernet";
    }
    if (type == "802-11-wireless") {
        return "wifi";
    }
    if (type == "vpn" || type == "wireguard") {
        return "vpn";
    }
    return "other";
}

} // namespace

const char* NetworkModule::kindName(Kind kind) {
    switch (kind) {
        case Kind::Loopback:
            return "loopback";
        case Kind::Ethernet:
            return "ethernet";
        case Kind::Wireless:
            return "wifi";
        case Kind::Virtual:
            return "virtual";
    }
    return "virtual";
}

const char* NetworkModule::securityName(std::uint32_t flags, std::uint32_t wpaFlags, std::uint32_t rsnFlags) {
    constexpr std::uint32_t keyManagement8021x = 0x200;
    constexpr std::uint32_t keyManagementSae = 0x400;
    constexpr std::uint32_t keyManagementOwe = 0x800;
    constexpr std::uint32_t privacy = 0x1;
    if ((rsnFlags & keyManagementSae) != 0U) {
        return "WPA3";
    }
    if ((rsnFlags & keyManagementOwe) != 0U) {
        return "OWE";
    }
    if (((rsnFlags | wpaFlags) & keyManagement8021x) != 0U) {
        return "WPA2 Enterprise";
    }
    if (rsnFlags != 0U) {
        return "WPA2";
    }
    if (wpaFlags != 0U) {
        return "WPA";
    }
    if ((flags & privacy) != 0U) {
        return "WEP";
    }
    return "Open";
}

NetworkModule::NetworkModule(const SystemPaths& paths, sd_event* event, SystemBus& systemBus)
    : paths_(paths), event_(event), systemBus_(systemBus) {
    historySeries_[0] = HistorySeries{"download", &downloadHistory_};
    historySeries_[1] = HistorySeries{"upload", &uploadHistory_};
}

NetworkModule::~NetworkModule() {
    stopNetworkManagerWatch();
    closeNetlink();
}

void NetworkModule::setTier(Tier tier) {
    const Tier previous = tier_;
    Module::setTier(tier);
    if (tier == Tier::None) {
        netDevFile_.close();
        closeNetlink();
        stopNetworkManagerWatch();
        interfaces_.clear();
        buffer_.clear();
        buffer_.shrink_to_fit();
        lastSample_ = 0;
        return;
    }
    if (!netDevFile_.isOpen()) {
        netDevFile_.open(paths_.proc + "/net/dev");
    }
    if (previous == Tier::None) {
        openNetlink();
        startNetworkManagerWatch();
        linksDirty_ = true;
    }
    if (tier == Tier::Detail && previous != Tier::Detail) {
        addressesDirty_ = true;
    }
}

void NetworkModule::resetBaselines() {
    for (Interface& interface : interfaces_) {
        interface.hasPrevious = false;
    }
    lastSample_ = 0;
}

// ---- rtnetlink -----------------------------------------------------------------------------

void NetworkModule::openNetlink() {
    if (netlinkFd_ >= 0) {
        return;
    }
    netlinkFd_ = ::socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (netlinkFd_ < 0) {
        return;
    }
    sockaddr_nl address{};
    address.nl_family = AF_NETLINK;
    address.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR | RTMGRP_IPV4_ROUTE;
    if (::bind(netlinkFd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        closeNetlink();
        return;
    }
    sd_event_source* source = nullptr;
    if (sd_event_add_io(event_, &source, netlinkFd_, EPOLLIN, &NetworkModule::handleNetlink, this) < 0) {
        closeNetlink();
        return;
    }
    netlinkSource_.reset(source);
}

void NetworkModule::closeNetlink() {
    netlinkSource_.reset();
    if (netlinkFd_ >= 0) {
        ::close(netlinkFd_);
        netlinkFd_ = -1;
    }
}

int NetworkModule::handleNetlink(sd_event_source* /*source*/, int fd, std::uint32_t /*events*/, void* userData) {
    auto* module = static_cast<NetworkModule*>(userData);
    std::array<char, 8192> buffer{};
    while (::recv(fd, buffer.data(), buffer.size(), MSG_DONTWAIT) > 0) {
    }
    module->linksDirty_ = true;
    module->addressesDirty_ = true;
    return 0;
}

// ---- Interfaces -----------------------------------------------------------------------------

NetworkModule::Interface& NetworkModule::findOrAddInterface(std::string_view name) {
    for (Interface& interface : interfaces_) {
        if (interface.name == name) {
            return interface;
        }
    }
    Interface& added = interfaces_.emplace_back();
    added.name = std::string(name);
    linksDirty_ = true;
    addressesDirty_ = true;
    return added;
}

std::string NetworkModule::defaultRouteInterface() const {
    const auto text = readTextFile(paths_.proc + "/net/route");
    if (!text) {
        return {};
    }
    std::string_view remaining = *text;
    std::string_view line;
    takeLine(remaining, line); // header
    std::string best;
    std::int64_t bestMetric = INT64_MAX;
    while (takeLine(remaining, line)) {
        std::string_view rest = line;
        const std::string_view interface = takeToken(rest);
        const std::string_view destination = takeToken(rest);
        takeToken(rest); // gateway
        takeToken(rest); // flags
        takeToken(rest); // refcnt
        takeToken(rest); // use
        const auto metric = parseNumber<std::int64_t>(takeToken(rest));
        const std::string_view mask = takeToken(rest);
        if (destination == "00000000" && mask == "00000000" && metric && *metric < bestMetric) {
            bestMetric = *metric;
            best = std::string(interface);
        }
    }
    return best;
}

void NetworkModule::refreshLinks(bool withAddresses) {
    const std::string classRoot = paths_.sys + "/class/net/";
    for (Interface& interface : interfaces_) {
        const std::string directory = classRoot + interface.name;
        if (interface.name == "lo") {
            interface.kind = Kind::Loopback;
        } else if (fileExists(directory + "/wireless") || fileExists(directory + "/phy80211")) {
            interface.kind = Kind::Wireless;
        } else if (fileExists(directory + "/device")) {
            interface.kind = Kind::Ethernet;
        } else {
            interface.kind = Kind::Virtual;
        }
        interface.containerLink = fileExists(directory + "/bridge") || fileExists(directory + "/brport");
        const auto state = readTextFile(directory + "/operstate");
        interface.up = state && (trim(*state) == "up" || trim(*state) == "unknown");
    }
    routeInterface_ = defaultRouteInterface();
    if (!withAddresses) {
        return;
    }
    for (Interface& interface : interfaces_) {
        interface.ipv4.clear();
        interface.ipv6.clear();
        interface.mac.clear();
    }
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) {
        return;
    }
    const std::unique_ptr<ifaddrs, void (*)(ifaddrs*)> guard(list, &::freeifaddrs);
    for (const ifaddrs* entry = list; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_name == nullptr) {
            continue;
        }
        Interface* interface = nullptr;
        for (Interface& candidate : interfaces_) {
            if (candidate.name == entry->ifa_name) {
                interface = &candidate;
                break;
            }
        }
        if (interface == nullptr) {
            continue;
        }
        std::array<char, INET6_ADDRSTRLEN> text{};
        const int family = entry->ifa_addr->sa_family;
        if (family == AF_INET) {
            const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
            if (::inet_ntop(AF_INET, &address->sin_addr, text.data(), text.size()) != nullptr) {
                interface->ipv4.emplace_back(text.data());
            }
        } else if (family == AF_INET6) {
            const auto* address = reinterpret_cast<const sockaddr_in6*>(entry->ifa_addr);
            if (IN6_IS_ADDR_LINKLOCAL(&address->sin6_addr)) {
                continue;
            }
            if (::inet_ntop(AF_INET6, &address->sin6_addr, text.data(), text.size()) != nullptr) {
                interface->ipv6.emplace_back(text.data());
            }
        } else if (family == AF_PACKET) {
            const auto* link = reinterpret_cast<const sockaddr_ll*>(entry->ifa_addr);
            if (link->sll_halen == 6) {
                std::array<char, 18> mac{};
                std::snprintf(mac.data(), mac.size(), "%02x:%02x:%02x:%02x:%02x:%02x", link->sll_addr[0],
                              link->sll_addr[1], link->sll_addr[2], link->sll_addr[3], link->sll_addr[4],
                              link->sll_addr[5]);
                interface->mac = mac.data();
            }
        }
    }
}

const NetworkModule::Interface* NetworkModule::primary() const {
    const auto byName = [this](const std::string& name) -> const Interface* {
        if (name.empty()) {
            return nullptr;
        }
        for (const Interface& interface : interfaces_) {
            if (interface.present && interface.name == name) {
                return &interface;
            }
        }
        return nullptr;
    };
    if (const Interface* found = byName(networkManager_.primaryInterface)) {
        return found;
    }
    if (const Interface* found = byName(routeInterface_)) {
        return found;
    }
    for (const Interface& interface : interfaces_) {
        if (interface.present && interface.up &&
            (interface.kind == Kind::Ethernet || interface.kind == Kind::Wireless)) {
            return &interface;
        }
    }
    return nullptr;
}

void NetworkModule::sample(const SampleContext& context) {
    if (networkManagerDirty_) {
        refreshNetworkManager();
    }
    const auto text = netDevFile_.readAll(buffer_);
    if (!text) {
        return;
    }
    const std::uint64_t elapsed = lastSample_ != 0 ? context.nowMicroseconds - lastSample_ : 0;
    lastSample_ = context.nowMicroseconds;
    for (Interface& interface : interfaces_) {
        interface.present = false;
    }
    parseNetDev(*text, [&](std::string_view interfaceName, const InterfaceCounters& counters) {
        Interface& interface = findOrAddInterface(interfaceName);
        interface.present = true;
        interface.current = counters;
        if (interface.hasPrevious && elapsed > 0) {
            interface.downloadRate = ratePerSecond(interface.previous.receivedBytes, counters.receivedBytes, elapsed);
            interface.uploadRate = ratePerSecond(interface.previous.sentBytes, counters.sentBytes, elapsed);
        } else {
            interface.downloadRate = 0;
            interface.uploadRate = 0;
        }
        interface.previous = counters;
        interface.hasPrevious = true;
    });
    const std::size_t before = interfaces_.size();
    std::erase_if(interfaces_, [](const Interface& interface) { return !interface.present; });
    if (interfaces_.size() != before) {
        linksDirty_ = true;
    }
    if (linksDirty_ || (context.tier == Tier::Detail && addressesDirty_)) {
        const bool withAddresses = context.tier == Tier::Detail;
        refreshLinks(withAddresses);
        linksDirty_ = false;
        if (withAddresses) {
            addressesDirty_ = false;
        }
    }
    if (const Interface* main = primary()) {
        downloadHistory_.push(context.nowMicroseconds, main->downloadRate);
        uploadHistory_.push(context.nowMicroseconds, main->uploadRate);
    }
}

void NetworkModule::encode(DictWriter& writer, const EncodeContext& context) const {
    const Interface* main = primary();
    writer.text("interface", main != nullptr ? main->name.c_str() : "");
    writer.number("download", main != nullptr ? main->downloadRate : 0);
    writer.number("upload", main != nullptr ? main->uploadRate : 0);
    writer.count("totalDownloaded", main != nullptr ? main->current.receivedBytes : 0);
    writer.count("totalUploaded", main != nullptr ? main->current.sentBytes : 0);
    writer.flag("connected", main != nullptr && main->up);
    writer.text("connectivity", connectivityName(networkManager_.connectivity));
    writer.text("connectionType", networkManager_.connectionType.empty()
                                      ? (main != nullptr ? kindName(main->kind) : "")
                                      : networkManager_.connectionType.c_str());
    if (networkManager_.wireless && !networkManager_.ssid.empty()) {
        writer.text("ssid", networkManager_.ssid.c_str());
        writer.number("signal", networkManager_.signal);
    }
    if (context.tier != Tier::Detail) {
        return;
    }
    writer.text("connectionName", networkManager_.connectionName.c_str());
    if (networkManager_.wireless) {
        writer.dictionary("wifi", [this](DictWriter& wifi) {
            wifi.text("ssid", networkManager_.ssid.c_str());
            wifi.number("signal", networkManager_.signal);
            wifi.number("frequency", networkManager_.frequency);
            wifi.number("bitrate", networkManager_.bitrate);
            wifi.text("security", networkManager_.security.c_str());
            const double frequency = networkManager_.frequency;
            wifi.text("band", frequency <= 0 ? "" : frequency < 3000 ? "2.4 GHz" : frequency < 5925 ? "5 GHz" : "6 GHz");
        });
    }
    writer.list("interfaces", [this, main](ListWriter& items) {
        for (const Interface& interface : interfaces_) {
            // The main link is always listed, even when it is a bridge (a LAN bridge can be the main link).
            // Otherwise loopback, container bridges with their veth ports, and idle tunnels are noise.
            const bool idleTunnel = interface.kind == Kind::Virtual
                                    && (!interface.up || (interface.ipv4.empty() && interface.ipv6.empty()));
            if (&interface != main && (interface.kind == Kind::Loopback || interface.containerLink || idleTunnel)) {
                continue;
            }
            items.item([&interface](DictWriter& item) {
                item.text("name", interface.name.c_str());
                item.text("kind", kindName(interface.kind));
                item.flag("up", interface.up);
                item.number("download", interface.downloadRate);
                item.number("upload", interface.uploadRate);
                item.count("totalDownloaded", interface.current.receivedBytes);
                item.count("totalUploaded", interface.current.sentBytes);
                item.text("mac", interface.mac.c_str());
                std::vector<const char*> addresses;
                addresses.reserve(interface.ipv4.size());
                for (const std::string& address : interface.ipv4) {
                    addresses.push_back(address.c_str());
                }
                item.texts("ipv4", addresses);
                addresses.clear();
                for (const std::string& address : interface.ipv6) {
                    addresses.push_back(address.c_str());
                }
                item.texts("ipv6", addresses);
            });
        }
    });
}

void NetworkModule::encodeInfo(DictWriter& writer) {
    std::vector<std::string> names;
    std::vector<const char*> pointers;
    for (const std::string& entry : listDirectory(paths_.sys + "/class/net")) {
        if (entry != "lo") {
            names.push_back(entry);
        }
    }
    pointers.reserve(names.size());
    for (const std::string& name : names) {
        pointers.push_back(name.c_str());
    }
    writer.texts("interfaces", pointers);
}

// ---- NetworkManager -----------------------------------------------------------------------

void NetworkModule::startNetworkManagerWatch() {
    sd_bus* bus = systemBus_.get();
    if (bus == nullptr || !networkManagerMatches_.empty()) {
        return;
    }
    // Only the interfaces we read. Device.Statistics (which can fire every second) is excluded.
    static constexpr const char* interfaces[] = {
        "org.freedesktop.NetworkManager",
        "org.freedesktop.NetworkManager.Connection.Active",
        "org.freedesktop.NetworkManager.Device",
        "org.freedesktop.NetworkManager.Device.Wireless",
        "org.freedesktop.NetworkManager.AccessPoint",
    };
    for (const char* interface : interfaces) {
        std::string rule = "type='signal',sender='org.freedesktop.NetworkManager',"
                           "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',arg0='";
        rule += interface;
        rule += "'";
        sd_bus_slot* slot = nullptr;
        if (sd_bus_add_match_async(bus, &slot, rule.c_str(), &NetworkModule::handleNetworkManagerChanged, nullptr,
                                   this) >= 0) {
            networkManagerMatches_.emplace_back(slot);
        }
    }
    // NetworkManager restarting.
    sd_bus_slot* slot = nullptr;
    if (sd_bus_add_match_async(bus, &slot,
                               "type='signal',sender='org.freedesktop.DBus',member='NameOwnerChanged',"
                               "arg0='org.freedesktop.NetworkManager'",
                               &NetworkModule::handleNetworkManagerChanged, nullptr, this) >= 0) {
        networkManagerMatches_.emplace_back(slot);
    }
    networkManagerDirty_ = true;
}

void NetworkModule::stopNetworkManagerWatch() {
    networkManagerMatches_.clear();
    networkManagerCall_.reset();
    networkManagerBusy_ = false;
    networkManager_ = NetworkManagerState{};
}

int NetworkModule::handleNetworkManagerChanged(sd_bus_message* /*message*/, void* userData, sd_bus_error* /*error*/) {
    static_cast<NetworkModule*>(userData)->networkManagerDirty_ = true;
    return 0;
}

void NetworkModule::refreshNetworkManager() {
    if (networkManagerBusy_) {
        return; // the running chain will see the dirty flag when it finishes
    }
    sd_bus* bus = systemBus_.get();
    if (bus == nullptr) {
        networkManagerDirty_ = false;
        return;
    }
    networkManagerDirty_ = false;
    networkManagerBusy_ = true;
    pending_ = NetworkManagerState{};
    if (getAllPropertiesAsync(bus, networkManagerCall_, networkManagerService, networkManagerPath,
                              "org.freedesktop.NetworkManager", &NetworkModule::handleManagerProperties, this) < 0) {
        networkManagerBusy_ = false;
    }
}

int NetworkModule::handleManagerProperties(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<NetworkModule*>(userData);
    if (sd_bus_message_is_method_error(message, nullptr)) {
        module->finishNetworkManagerRefresh(); // NetworkManager not running
        return 0;
    }
    module->pending_.available = true;
    std::string primaryConnection;
    readProperties(message, [&](std::string_view key, VariantValue& value) {
        if (key == "PrimaryConnection") {
            primaryConnection = value.text().value_or("/");
        } else if (key == "Connectivity") {
            module->pending_.connectivity = static_cast<std::uint32_t>(value.integer().value_or(0));
        }
    });
    if (primaryConnection.empty() || primaryConnection == "/" ||
        getAllPropertiesAsync(module->systemBus_.get(), module->networkManagerCall_, networkManagerService,
                              primaryConnection.c_str(), "org.freedesktop.NetworkManager.Connection.Active",
                              &NetworkModule::handleActiveConnection, module) < 0) {
        module->finishNetworkManagerRefresh();
    }
    return 0;
}

int NetworkModule::handleActiveConnection(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<NetworkModule*>(userData);
    if (sd_bus_message_is_method_error(message, nullptr)) {
        module->finishNetworkManagerRefresh();
        return 0;
    }
    std::string device;
    readProperties(message, [&](std::string_view key, VariantValue& value) {
        if (key == "Id") {
            std::array<char, 256> name{};
            copySanitizedUtf8(value.text().value_or(""), name.data(), name.size());
            module->pending_.connectionName = name.data();
        } else if (key == "Type") {
            module->pending_.connectionType = connectionTypeFromNetworkManager(value.text().value_or(""));
        } else if (key == "Devices") {
            const auto devices = value.texts();
            if (devices && !devices->empty()) {
                device = devices->front();
            }
        }
    });
    module->pendingDevicePath_ = device;
    if (device.empty() ||
        getAllPropertiesAsync(module->systemBus_.get(), module->networkManagerCall_, networkManagerService,
                              device.c_str(), "org.freedesktop.NetworkManager.Device", &NetworkModule::handleDevice,
                              module) < 0) {
        module->finishNetworkManagerRefresh();
    }
    return 0;
}

int NetworkModule::handleDevice(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<NetworkModule*>(userData);
    if (sd_bus_message_is_method_error(message, nullptr)) {
        module->finishNetworkManagerRefresh();
        return 0;
    }
    std::uint32_t deviceType = 0;
    readProperties(message, [&](std::string_view key, VariantValue& value) {
        if (key == "IpInterface" || (key == "Interface" && module->pending_.primaryInterface.empty())) {
            const auto name = value.text();
            if (name && !name->empty()) {
                module->pending_.primaryInterface = *name;
            }
        } else if (key == "DeviceType") {
            deviceType = static_cast<std::uint32_t>(value.integer().value_or(0));
        }
    });
    module->pending_.wireless = deviceType == nmDeviceTypeWifi;
    if (!module->pending_.wireless ||
        getAllPropertiesAsync(module->systemBus_.get(), module->networkManagerCall_, networkManagerService,
                              module->pendingDevicePath_.c_str(), "org.freedesktop.NetworkManager.Device.Wireless",
                              &NetworkModule::handleWirelessDevice, module) < 0) {
        module->finishNetworkManagerRefresh();
    }
    return 0;
}

int NetworkModule::handleWirelessDevice(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<NetworkModule*>(userData);
    if (sd_bus_message_is_method_error(message, nullptr)) {
        module->finishNetworkManagerRefresh();
        return 0;
    }
    std::string accessPoint;
    readProperties(message, [&](std::string_view key, VariantValue& value) {
        if (key == "ActiveAccessPoint") {
            accessPoint = value.text().value_or("/");
        } else if (key == "Bitrate") {
            module->pending_.bitrate = static_cast<double>(value.integer().value_or(0)) / 1000.0;
        }
    });
    if (accessPoint.empty() || accessPoint == "/" ||
        getAllPropertiesAsync(module->systemBus_.get(), module->networkManagerCall_, networkManagerService,
                              accessPoint.c_str(), "org.freedesktop.NetworkManager.AccessPoint",
                              &NetworkModule::handleAccessPoint, module) < 0) {
        module->finishNetworkManagerRefresh();
    }
    return 0;
}

int NetworkModule::handleAccessPoint(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<NetworkModule*>(userData);
    if (!sd_bus_message_is_method_error(message, nullptr)) {
        std::uint32_t flags = 0;
        std::uint32_t wpaFlags = 0;
        std::uint32_t rsnFlags = 0;
        readProperties(message, [&](std::string_view key, VariantValue& value) {
            if (key == "Ssid") {
                const auto bytes = value.bytes();
                if (bytes) {
                    std::array<char, 128> ssid{};
                    copySanitizedUtf8(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()),
                                      ssid.data(), ssid.size());
                    module->pending_.ssid = ssid.data();
                }
            } else if (key == "Strength") {
                module->pending_.signal = value.number().value_or(0);
            } else if (key == "Frequency") {
                module->pending_.frequency = value.number().value_or(0);
            } else if (key == "Flags") {
                flags = static_cast<std::uint32_t>(value.integer().value_or(0));
            } else if (key == "WpaFlags") {
                wpaFlags = static_cast<std::uint32_t>(value.integer().value_or(0));
            } else if (key == "RsnFlags") {
                rsnFlags = static_cast<std::uint32_t>(value.integer().value_or(0));
            }
        });
        module->pending_.security = securityName(flags, wpaFlags, rsnFlags);
    }
    module->finishNetworkManagerRefresh();
    return 0;
}

void NetworkModule::finishNetworkManagerRefresh() {
    networkManager_ = pending_;
    networkManagerBusy_ = false;
    networkManagerCall_.reset();
    // A change arrived while the chain ran: it is picked up on the next sample.
}

} // namespace readout
