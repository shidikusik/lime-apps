#!/bin/sh
# Run on Debian/Ubuntu with build-essential, pkg-config, libgtk-3-dev,
# dpkg-dev, fakeroot and binutils installed. No root privileges are required.
set -eu
repository_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repository_dir"
package_version=$(sed -n 's/^VERSION := //p' Makefile)
package_arch=$(dpkg --print-architecture)
output_dir=${1:-dist}
mkdir -p "$output_dir"
output_dir=$(CDPATH= cd -- "$output_dir" && pwd)
package_work=$(mktemp -d)
trap 'rm -rf "$package_work"' EXIT HUP INT TERM
package_root=$package_work/root
make -j"${JOBS:-2}" all
make install DESTDIR="$package_root" PREFIX=/usr
for binary in "$package_root"/usr/bin/lime-*; do strip --strip-unneeded "$binary"; done
# dpkg-shlibdeps derives real shared-library minimums from the build system.
mkdir -p "$package_work/debian" "$package_root/DEBIAN"
cat > "$package_work/debian/control" <<CONTROL
Source: lime-apps
Section: utils
Priority: optional
Maintainer: shidikusik <200198680+shidikusik@users.noreply.github.com>

Package: lime-apps
Architecture: any
Description: Lightweight native desktop applications for LimeOS
CONTROL
set --
for binary in "$package_root"/usr/bin/lime-*; do set -- "$@" "-e$binary"; done
package_dependencies=$(cd "$package_work" && dpkg-shlibdeps -O "$@")
package_dependencies=${package_dependencies#shlibs:Depends=}
package_size=$(du -sk "$package_root/usr" | awk '{print $1}')
cat > "$package_root/DEBIAN/control" <<CONTROL
Package: lime-apps
Version: $package_version
Architecture: $package_arch
Maintainer: shidikusik <200198680+shidikusik@users.noreply.github.com>
Section: utils
Priority: optional
Homepage: https://github.com/shidikusik/lime-apps
Depends: $package_dependencies, librsvg2-common, shared-mime-info
Installed-Size: $package_size
Description: Lightweight native desktop applications for LimeOS
 Five small C and GTK3 applications: LimeCalc, LimeNotes, LimeTasks,
 LimeView and LimeClock. No background service or web runtime.
CONTROL
# No maintainer scripts: removal never touches a user's notes or tasks.
dpkg-deb --root-owner-group --build "$package_root" \
    "$output_dir/lime-apps_${package_version}_${package_arch}.deb"
