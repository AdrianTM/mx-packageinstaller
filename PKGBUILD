# Maintainer: Adrian <adrian@mxlinux.org>
pkgname=mx-packageinstaller
pkgver=${PKGVER:-25.12.4}
pkgrel=1
pkgdesc="MX Package Installer - a tool for managing packages and Flatpaks"
arch=('x86_64' 'i686')
url="https://mxlinux.org"
license=('GPL3')
depends=('qt6-base' 'polkit' 'flatpak' 'socat')
makedepends=('cmake' 'ninja' 'qt6-tools')
optdepends=('paru: AUR helper for AUR tab operations')
source=()
sha256sums=()

build() {
    cd "${startdir}"

    if [ ! -f build/mx-packageinstaller ]; then
        cmake -G Ninja \
            -B build \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX=/usr \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

        cmake --build build --parallel
    else
        echo "Binary already built, skipping build step..."
    fi
}

package() {
    cd "${startdir}"

    install -Dm755 build/mx-packageinstaller "${pkgdir}/usr/bin/mx-packageinstaller"

    install -dm755 "${pkgdir}/usr/share/mx-packageinstaller/locale"
    install -Dm644 build/*.qm "${pkgdir}/usr/share/mx-packageinstaller/locale/" 2>/dev/null || true

    install -dm755 "${pkgdir}/usr/lib/mx-packageinstaller"
    install -Dm755 scripts/helper "${pkgdir}/usr/lib/mx-packageinstaller/helper"
    install -Dm755 scripts/mxpi-lib "${pkgdir}/usr/lib/mx-packageinstaller/mxpi-lib"

    install -Dm644 scripts/org.mxlinux.pkexec.mxpi-helper.policy \
        "${pkgdir}/usr/share/polkit-1/actions/org.mxlinux.pkexec.mxpi-helper.policy"

    install -Dm644 mx-packageinstaller.desktop "${pkgdir}/usr/share/applications/mx-packageinstaller.desktop"

    install -Dm644 icons/mx-packageinstaller.png "${pkgdir}/usr/share/icons/hicolor/48x48/apps/mx-packageinstaller.png"
    install -Dm644 icons/mx-packageinstaller.png "${pkgdir}/usr/share/pixmaps/mx-packageinstaller.png"
    install -Dm644 icons/mx-packageinstaller.svg "${pkgdir}/usr/share/icons/hicolor/scalable/apps/mx-packageinstaller.svg"

    install -dm755 "${pkgdir}/usr/share/doc/mx-packageinstaller"
    if [ -d help ]; then
        cp -r help/* "${pkgdir}/usr/share/doc/mx-packageinstaller/" 2>/dev/null || true
    fi

}
