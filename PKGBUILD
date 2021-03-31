# Maintainer: Tom < reztho at archlinux dot us >
# Based on an AUR contribution of: Juraj Misur <juraj.misur@gmail.com>
# Contributors:
# - veger
# - Limych
pkgname=pi-capt
pkgver=2.71
pkgrel=3
pkgdesc="Canon CAPT Printer Driver for Linux. Compiled from source code."
arch=('i686' 'x86_64', 'arm64')
url='http://support-asia.canon-asia.com/'
license=('custom')
depends=('cups' 'glib2' 'libglade' 'gtk2' 'atk' 'libxml2' 'popt' 'ghostscript')
depends_x86_64=('lib32-libxml2' 'lib32-popt' 'lib32-gcc-libs')
install=${pkgname}.install
_tardir=linux-capt-drv-v271-uken
source=("http://gdlp01.c-wss.com/gds/6/0100004596/05/${_tardir}.tar.gz"
        'ccpd.service')
options=(!strip !zipman !buildflags)
backup=('etc/ccpd.conf')

_pkgcommonver=3.21
_endlibdir=/usr/lib

prepare() {
    cd ${srcdir}
    tar xvzf ${srcdir}/${_tardir}/src/cndrvcups-common-${_pkgcommonver}-1.tar.gz
#     tar xvzf ${srcdir}/${_tardir}/src/cndrvcups-capt-${pkgver}-1.tar.gz
}

_build_cndrvcups_common() {
    _common_dir=${srcdir}/cndrvcups-common-${_pkgcommonver}

    msg "cndrvcups-common package"
    msg "Configuring cndrvcups-common package"
    msg "Configuring: buftool"
    cd ${_common_dir}/buftool && /usr/bin/autoreconf -fi && ./autogen.sh --prefix=/usr --libdir=/usr/lib
    msg "Configuring: cngplp"
    cd ${_common_dir}/cngplp && /usr/bin/autoreconf -fi && LDFLAGS='-z muldefs' LIBS='-lgmodule-2.0 -lgtk-x11-2.0 -lglib-2.0 -lgobject-2.0' ./autogen.sh --prefix=/usr --libdir=/usr/lib
    msg "Configuring: backend"
    cd ${_common_dir}/backend && /usr/bin/autoreconf -fi && ./autogen.sh --prefix=/usr --libdir=/usr/lib

    msg "Compiling cndrvcups-common package"
    cd ${_common_dir}
    make

    cd ${_common_dir}/c3plmod_ipc
    make
}

_package_cndrvcups_common() {
    _common_dir=${srcdir}/cndrvcups-common-${_pkgcommonver}

    msg "Installing cndrvcups-common package"
    for _dir in buftool cngplp backend
    do
        msg "Installing: $_dir"
        cd ${_common_dir}/$_dir && make DESTDIR=${pkgdir} install
    done

    msg "Installing: c3plmod_ipc"
    cd ${_common_dir}/c3plmod_ipc/
    make install DESTDIR=${pkgdir} LIBDIR=/usr/lib
    
    cd ${_common_dir}
    install -dm755 ${pkgdir}/usr/bin
    install -c -m 755 libs/c3pldrv ${pkgdir}/usr/bin
    install -dm755 ${pkgdir}${_endlibdir}
    install -c -m 755 libs/libcaiowrap.so.1.0.0 ${pkgdir}${_endlibdir}
    install -c -m 755 libs/libcaiousb.so.1.0.0 ${pkgdir}${_endlibdir}
    install -c -m 755 libs/libc3pl.so.0.0.1 ${pkgdir}${_endlibdir}
    install -c -m 755 libs/libcaepcm.so.1.0 ${pkgdir}${_endlibdir}
    install -c -m 755 libs/libColorGear.so.0.0.0 ${pkgdir}${_endlibdir}
    install -c -m 755 libs/libColorGearC.so.0.0.0 ${pkgdir}${_endlibdir}
    install -c -m 755 libs/libcanon_slim.so.1.0.0 ${pkgdir}${_endlibdir}

    cd ${pkgdir}${_endlibdir}
    ln -s libc3pl.so.0.0.1 libc3pl.so.0
    ln -s libc3pl.so.0.0.1 libc3pl.so
    ln -s libcaepcm.so.1.0 libcaepcm.so.1
    ln -s libcaepcm.so.1.0 libcaepcm.so
    ln -s libcaiowrap.so.1.0.0 libcaiowrap.so.1
    ln -s libcaiowrap.so.1.0.0 libcaiowrap.so
    ln -s libcaiousb.so.1.0.0 libcaiousb.so.1
    ln -s libcaiousb.so.1.0.0 libcaiousb.so
    ln -s libcanonc3pl.so.1.0.0 libcanonc3pl.so.1
    ln -s libcanonc3pl.so.1.0.0 libcanonc3pl.so
    ln -s libcanon_slim.so.1.0.0 libcanon_slim.so.1
    ln -s libcanon_slim.so.1.0.0 libcanon_slim.so
    ln -s libColorGear.so.0.0.0 libColorGear.so.0
    ln -s libColorGear.so.0.0.0 libColorGear.so
    ln -s libColorGearC.so.0.0.0 libColorGearC.so.0
    ln -s libColorGearC.so.0.0.0 libColorGearC.so

    install -dm755 ${pkgdir}/usr/share/caepcm
    
    cd ${_common_dir}
    install -c -m 644 data/*.ICC  ${pkgdir}/usr/share/caepcm
}

_build_capt() {
    _capt_dir=${srcdir}/capt

    msg "capt package"

    msg "Compiling capt package"
    cd ${_capt_dir}
    make
}

_package_capt() {
    _capt_dir=${srcdir}/capt

    msg "Installing capt package"
    cd ${_capt_dir}
    make install
}

package() {
    _build_cndrvcups_common
    _package_cndrvcups_common
    _build_capt
    _package_capt

    # Other dirs...
    cd ${pkgdir}/usr/share/cups/model
    for fn in CN*CAPT*.ppd; do
  	    ln -s /usr/share/cups/model/${fn} ${pkgdir}/usr/share/ppd/cupsfilters/${fn}
    done
}

md5sums=('2421628aac9c6000d08c46a1204f08be'
         '63dd8648eaa7a5ec8b603f3ac841e141')
