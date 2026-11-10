#pragma once

#include "bus/bus_util.h"
#include "common/paths.h"
#include "io/held_file.h"
#include "modules/module.h"
#include "parsers/kernel_text.h"

#include <array>
#include <string>
#include <vector>

namespace readout {

class NetworkModule final : public Module {
public:
    NetworkModule(const SystemPaths& paths, sd_event* event, SystemBus& systemBus);
    ~NetworkModule() override;
    NetworkModule(const NetworkModule&) = delete;
    NetworkModule& operator=(const NetworkModule&) = delete;
    NetworkModule(NetworkModule&&) = delete;
    NetworkModule& operator=(NetworkModule&&) = delete;

    [[nodiscard]] const char* name() const override { return "network"; }
    void setTier(Tier tier) override;
    void sample(const SampleContext& context) override;
    void encode(DictWriter& writer, const EncodeContext& context) const override;
    void encodeInfo(DictWriter& writer) override;
    [[nodiscard]] std::span<const HistorySeries> history() const override { return historySeries_; }
    void resetBaselines() override;

    enum class Kind : std::uint8_t { Loopback, Ethernet, Wireless, Virtual };
    static const char* kindName(Kind kind);
    // Security label from NetworkManager access point flags.
    static const char* securityName(std::uint32_t flags, std::uint32_t wpaFlags, std::uint32_t rsnFlags);

private:
    struct Interface {
        std::string name;
        Kind kind = Kind::Virtual;
        bool up = false;
        bool containerLink = false; // a bridge or bridge port (Docker, Podman, libvirt)
        bool present = false;
        bool hasPrevious = false;
        InterfaceCounters previous;
        InterfaceCounters current;
        double downloadRate = 0;
        double uploadRate = 0;
        std::string mac;
        std::vector<std::string> ipv4;
        std::vector<std::string> ipv6;
    };

    struct NetworkManagerState {
        bool available = false;
        std::string primaryInterface;
        std::string connectionName;
        std::string connectionType; // "ethernet", "wifi", "vpn", "other"
        std::uint32_t connectivity = 0; // NMConnectivityState
        bool wireless = false;
        std::string ssid;
        double signal = 0;
        double frequency = 0;
        double bitrate = 0; // Mbit/s
        std::string security;
    };

    // NetworkManager refresh chain (each step is an async GetAll).
    void startNetworkManagerWatch();
    void stopNetworkManagerWatch();
    void refreshNetworkManager();
    static int handleNetworkManagerChanged(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleManagerProperties(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleActiveConnection(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleDevice(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleWirelessDevice(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleAccessPoint(sd_bus_message* message, void* userData, sd_bus_error* error);
    void finishNetworkManagerRefresh();

    void openNetlink();
    void closeNetlink();
    static int handleNetlink(sd_event_source* source, int fd, std::uint32_t events, void* userData);

    void refreshLinks(bool withAddresses);
    [[nodiscard]] std::string defaultRouteInterface() const;
    Interface& findOrAddInterface(std::string_view name);
    [[nodiscard]] const Interface* primary() const;

    SystemPaths paths_;
    sd_event* event_;
    SystemBus& systemBus_;
    HeldFile netDevFile_;
    std::vector<char> buffer_;
    std::uint64_t lastSample_ = 0;
    std::vector<Interface> interfaces_;
    bool linksDirty_ = true;
    bool addressesDirty_ = true;
    std::string routeInterface_;

    int netlinkFd_ = -1;
    EventSourceHandle netlinkSource_;

    NetworkManagerState networkManager_;
    NetworkManagerState pending_;
    std::vector<SlotHandle> networkManagerMatches_;
    SlotHandle networkManagerCall_;
    std::string pendingDevicePath_;
    bool networkManagerDirty_ = true;
    bool networkManagerBusy_ = false;

    HistoryBuffer downloadHistory_;
    HistoryBuffer uploadHistory_;
    std::array<HistorySeries, 2> historySeries_{};
};

} // namespace readout
