Wayland EGL External Platform library
=====================================

Overview
--------

This is a work-in-progress implementation of a EGL External Platform library to
add client-side Wayland support to EGL on top of EGLDevice and EGLStream
families of extensions.

This library implements an EGL External Platform interface to work along with
EGL drivers that support the external platform mechanism. More information
about EGL External platforms and the interface can be found at:

https://github.com/NVIDIA/eglexternalplatform


Building and Installing the library
-----------------------------------

This library build-depends on:

 * EGL headers

       https://www.khronos.org/registry/EGL/

 * Wayland libraries & protocols

       https://wayland.freedesktop.org/

 * EGL External Platform interface

       https://github.com/NVIDIA/eglexternalplatform


To build, run:

    ./autogen.sh
    make


To install, run:

    make install


You can also use meson build system to build and install:

    meson builddir
    cd builddir
    ninja
    ninja install


*Notes*:

The NVIDIA EGL driver uses a JSON-based loader to load all EGL External
platforms available on the system.

If this library is not installed as part of a NVIDIA driver installation,
a JSON configuration file must be manually added in order to make the
library work with the NVIDIA driver.

The default EGL External platform JSON configuration directory is:

  `/usr/share/egl/egl_external_platform.d/`


Acknowledgements
----------------

Thanks to James Jones for the original implementation of the Wayland EGL
platform.


### Wayland EGL External platform library ###

The Wayland EGL External platform library itself is licensed as follows:

    Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.


### buildconf ###

The Wayland EGL External platform library uses the buildconf autotools
bootstrapping script 'autogen.sh':

http://freecode.com/projects/buildconf

This script carries the following copyright notice:

    Copyright (c) 2005-2009 United States Government as represented by
    the U.S. Army Research Laboratory.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following
    disclaimer in the documentation and/or other materials provided
    with the distribution.

    3. The name of the author may not be used to endorse or promote
    products derived from this software without specific prior written
    permission.

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
    OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
    GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
    NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
