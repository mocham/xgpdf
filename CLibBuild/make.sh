#! /usr/bin/bash
LDFLAGS="-Wl,--whole-archive -l:libglib-2.0.a -l:libpixman-1.a -l:libpoppler.a -Wl,--no-whole-archive -l:libturbojpeg.a -l:libgobject-2.0.a -l:liblcms2.a -l:libopenjp2.a -l:libfreetype.a -l:libcairo.a -l:libffi.a -l:libpoppler-glib.a -l:libstdc++.a -static-libgcc"
CFLAGS="-I/output/include/gmodule -I/output/include/gobject -I/output/include/glib -I/output/include"
SONAME="libpdf.so"
HNAME="wrapper-pdf.h"
CNAME="plugin-pdf.c"

docker run --rm -it -v./:/build --workdir /build --entrypoint /usr/bin/bash make -c "python wrapper-static.py _ $HNAME $CNAME && gcc -shared -fPIC -O3 $CFLAGS -o $SONAME $CNAME -L/output/static-libs $LDFLAGS"
