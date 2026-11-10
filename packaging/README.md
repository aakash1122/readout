# Packaging

Every distribution gets three packages.
The two frontends depend on the daemon package.

| Package | Contents |
|---|---|
| `readout` | `readoutd`, its systemd user unit and D-Bus activation file, and `readoutctl` |
| Plasma widget (`plasma-applet-readout`, Debian: `plasma-widget-readout`) | The applet plugin and its notification config |
| `gnome-shell-extension-readout` | The GNOME Shell extension, system-wide |

## Fedora (COPR)

`fedora/readout.spec` builds from the release tarball.
It uses `%autochangelog`, so the changelog comes from git history.

```sh
rpmbuild -ba packaging/fedora/readout.spec   # with the tarball in ~/rpmbuild/SOURCES
```

## Arch Linux (AUR)

`arch/PKGBUILD` is a split package.
Fill in `sha256sums` with `updpkgsums` after tagging a release.

## Debian and Ubuntu

Copy `packaging/debian` to `debian/` at the source root.
`debian/changelog` is not kept in the repository; create it for each release with `dch`:

```sh
cp -r packaging/debian debian
dch --create --package readout --newversion 0.1.0-1 --distribution unstable "Initial release."
dpkg-buildpackage -us -uc
```

Debian 13 (trixie) is the oldest supported release: it has GNOME 48 and Plasma 6.3.
