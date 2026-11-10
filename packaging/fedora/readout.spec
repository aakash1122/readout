Name:           readout
Version:        0.1.0
Release:        1%{?dist}
Summary:        System monitor for the KDE Plasma and GNOME Shell panels
License:        MIT
URL:            https://github.com/aakash1122/readout
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  python3
BuildRequires:  systemd-rpm-macros
BuildRequires:  pkgconfig(libsystemd)
BuildRequires:  gtest-devel
BuildRequires:  dbus-daemon
BuildRequires:  hwdata
BuildRequires:  extra-cmake-modules
BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6Quick)
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  cmake(KF6I18n)
BuildRequires:  cmake(KF6CoreAddons)
BuildRequires:  cmake(KF6Config)
BuildRequires:  cmake(Plasma)
BuildRequires:  glib2-devel
BuildRequires:  gettext

Requires:       hwdata
Requires:       hicolor-icon-theme
%{?systemd_requires}

%description
readoutd is a small session D-Bus service that reads CPU, memory, disk, network,
GPU, sensor, battery and Bluetooth statistics for the Readout panel widgets.
It reads only what is on screen, starts on demand and exits when unused.
This package also contains the readoutctl command-line client.

%package -n plasma-applet-readout
Summary:        Readout system monitor widget for KDE Plasma
Requires:       %{name}%{?_isa} = %{version}-%{release}
Requires:       kf6-kquickcharts
Requires:       kf6-kirigami
Requires:       kf6-kcmutils
Requires:       kf6-knotifications
Requires:       libplasma

%description -n plasma-applet-readout
Panel widget for KDE Plasma 6 showing CPU, memory, disk, network, GPU, sensors,
battery, Bluetooth and world clocks, with detailed popups and alerts.

%package -n gnome-shell-extension-readout
Summary:        Readout system monitor for GNOME Shell
BuildArch:      noarch
Requires:       %{name} = %{version}-%{release}
Requires:       gnome-shell >= 48

%description -n gnome-shell-extension-readout
GNOME Shell extension showing CPU, memory, disk, network, GPU, sensors,
battery, Bluetooth and world clocks in the top bar, with detailed popups.

%prep
%autosetup

%build
%cmake -G Ninja -DREADOUT_STATIC_LIBSTDCXX=OFF
%cmake_build

%install
%cmake_install

%check
%ctest

%post
%systemd_user_post readoutd.service

%preun
%systemd_user_preun readoutd.service

%files
%license LICENSE
%doc README.md docs/dbus-api.md
%{_bindir}/readoutctl
%{_libexecdir}/readoutd
%{_userunitdir}/readoutd.service
%{_datadir}/dbus-1/services/io.github.aakash1122.Readout1.service
%{_datadir}/dbus-1/interfaces/io.github.aakash1122.Readout1.xml
%{_datadir}/icons/hicolor/scalable/apps/io.github.aakash1122.readout.svg
%{_datadir}/icons/hicolor/symbolic/apps/io.github.aakash1122.readout-symbolic.svg

%files -n plasma-applet-readout
%license LICENSE
%{_qt6_plugindir}/plasma/applets/io.github.aakash1122.readout.so
%{_datadir}/knotifications6/readout.notifyrc

%files -n gnome-shell-extension-readout
%license LICENSE
%{_datadir}/gnome-shell/extensions/readout@aakash1122.github.io/

%changelog
%autochangelog
