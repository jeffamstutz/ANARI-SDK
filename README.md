![logo](https://github.com/KhronosGroup/ANARI-Docs/blob/main/images/anari_RGB_Mar20.svg)

ANARI-SDK
=========

This repository contains the source for the ANARI API SDK. This includes:

- [Front-end library + implementation guide](src/anari)
- [Device implementation utilties for implementations](src/helium)
- [Example device implementation](src/devices/helide) (not intended for production use)
- [Example applications](examples/)
- [Interactive sample viewer](examples/viewer)
- [Render tests](tests/render)
- [(experimental) OpenUSD Hydra render delegate plugin](src/hdanari)

The ANARI specification can be found on the Khronos website
[here](https://www.khronos.org/registry/ANARI/).

The SDK is tested on Linux, but is also intended to be usable on other operating
systems such as macOS and Windows.

If you find any problems with the SDK, please do not hesitate to
[open an issue](https://github.com/KhronosGroup/ANARI-SDK/issues/new) on this
project!

## Getting the SDK from vcpkg

The ANARI-SDK is available as the `anari` package in
[vcpkg](https://vcpkg.io/en/). Simply follow [these
instructions](https://vcpkg.io/en/getting-started) for setting up your
environment to use vcpkg, then run the following to get ANARI:

```bash
% vcpkg install anari
```

## Building the SDK from source

The repository uses CMake 3.11+ to build the library, example implementation,
sample apps, and tests. For example, to build (must be in a separate directory
from the source directory), you can do:

```bash
% cd /path/to/anari
% mkdir build
% cd build
% cmake ..
```

Using a tool like `ccmake` or `cmake-gui` will let you see which options are
available to enable. The following CMake options are offered:

- `BUILD_SHARED_LIBS`   : build everything as shared libraries or static libraries
- `BUILD_CAT`           : build the capability analysis tool
- `BUILD_CTS`           : build the conformance test suite
- `BUILD_TESTING`       : build unit and regression test binaries
- `BUILD_HELIDE_DEVICE` : build the provided example `helide` device implementation
- `BUILD_REMOTE_DEVICE` : build the provided experimental `remote` device implementation
- `BUILD_EXAMPLES`      : build example applications
- `BUILD_VIEWER`        : build viewer too (needs SDL3) if building examples
- `BUILD_HDANARI`       : build (experimental) OpenUSD Hydra delegate plugin

Once built, the library can be installed via the `install` target created by
CMake. This can be invoked from your build directory with (on any platform):

```bash
% cmake --build . -t install
```

## Using the SDK after install with CMake

The ANARI SDK exports CMake targets for the main front-end library and utilities
helper library. The targets which are exported are as follows:

- `anari::anari`        : dynamically link the main ANARI API library target
- `anari::anari_static` : statically link the main ANARI API library target
- `anari::helium`       : library containing base device implementation abstractions (static)

These targets can be found with CMake via `find_package(anari)`. The examples
are written such that they can be built stand alone from the SDK source tree.
The simplest usage can be found [here](examples/simple).

Note that `anari::helium` will still require linking either `anari::anari` or
`anari::anari_static` to give consumers the option whether to link the ANARI
API dynamically or statically.

The following additional helper library components can be requested by listing
them under `find_pacakge(anari)`:

- `viewer` : Source library target `anari::anari_viewer` for building small viewer apps
- `code_gen` : Enable the use of code generation CMake functions downstream

Both of these libraries are optionally installed and are not available to
downstream projects unless they are explicitly requested. To request one of
these components, simply add them to the `COMPONENTS` portion of `find_package`:

```cmake
find_package(anari COMPONENTS viewer code_gen)
```

## Running the examples

The basic tutorial app (built by default) uses the `helide` device as an
example, which can be run with:

```bash
% ./anariTutorial
```

Note that running the tutorial will require that the `helide` device is enabled
in your build with the CMake option `BUILD_HELIDE_DEVICE=ON`.

The viewer application (enabled with `BUILD_VIEWER=ON`) by default uses the
`environment` library, which reads `ANARI_LIBRARY` as an environment variable to
get the library to load. For example it can be run with:

```bash
% export ANARI_LIBRARY=helide
% ./anariViewer
```

Alternatively, either `--library` or `-l` can be used on the viewer's command
line to override the ANARI library to be loaded.

The regression test binary (`anariRenderTests`) used to render the test scenes
without a window (results saved out as PNG images) uses the same mechanisms as
the viewer to select/override which library is loaded at runtime.

## Available implementations

### SDK provided example implementation

An example device implementation [helide](src/devices/helide) is provided as a
starting point for users exploring the SDK and for implementors to see how the
API might be implemented. It implements a very simple ray tracing implementation
using Embree for intersection. Users should look to use vendor provided,
hardware-optimized ANARI implementations which are shipped independently from
the SDK. (see below)

### Using the debug device layer

The ANARI-SDK ships with a [debug layer](src/devices/debug) implemented as an
ordinary `ANARIDevice` which wraps a device (set as the `wrappedDevice`
parameter on the debug device). This device uses the object queries reported by
the wrapped device to validate correct usage of object subtypes, parameters, and
properties, as well as validate correct object lifetimes. The wrapped device is
then used to actually implement the ANARI API to allow applications to still
function normally.

The device can be created by using the normal library loading mechanics using
`anariLoadLibrary("debug", ...)`, creating a the debug device instances with,
`anariNewDevice(debugLibrary, "default")`, and then creating a device to use
and set it on the device with
`anariSetParameter(debugDevice, "wrappedDevice", ANARI_DEVICE, &wrappedHandle)`.

As a convenience, users can use the `ANARI_DEBUG_WRAPPED_LIBRARY` environment
variable to have the debug device do the mechanics of loading and creating the
underlying wrapped library. For example, you can do the following to run the
viewer with the debug device wrapping `helide`:

```
% ANARI_LIBRARY=debug ANARI_DEBUG_WRAPPED_LIBRARY=helide ./anariViewer
```

Or using the `-l` syntax for specifying the library:

```
% ANARI_DEBUG_WRAPPED_LIBRARY=helide ./anariViewer -l debug
```

See the [simple debug device turorial](examples/simple/anariTutorialDebug.c) for
how to setup the debug device without using environment variables.

Note that if `ANARI_DEBUG_WRAPPED_LIBRARY` is set, it will take priority over
programatically set wrapped devices.

Tracing features of the debug device can be set using the following environment
variables:
- `ANARI_DEBUG_TRACE_MODE` sets the tracing mode, only `code` is supported for now.
- `ANARI_DEBUG_TRACE_DIR` set the folder where the trace will be dumped.

### (Unofficial) list of actively developed ANARI implementations

- [Barney](https://github.com/ingowald/barney) (MPI distributed renderer)
- [ANARI-PTC](https://github.com/ingowald/ANARI-PTC) (MPI distributed adapter device)
- [Cycles (Blender)](https://github.com/jeffamstutz/anari-cycles)
- [Intel OSPRay](https://github.com/ospray/anari-ospray)
- [NVIDIA USD](https://github.com/NVIDIA-Omniverse/AnariUsdDevice)
- [NVIDIA VisRTX + VisGL](https://github.com/NVIDIA/VisRTX)
- [Visionaray](https://github.com/szellmann/anari-visionaray)
- [VTK-m](https://github.com/dpugmire/anari-library-vtkm)

If you implement a backend to the ANARI SDK, please open a PR to add it to this
list!

### list of ANARI implementations no longer maintained

- [AMD RadeonProRender](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderANARI)

## ANARI-enabled applications

### Publicly available applications + libraries using ANARI

- [Blender](https://www.blender.org/) (via [addon](examples/blender) found in the ANARI-SDK)
- [COVISE](https://www.hlrs.de/solutions/types-of-computing/visualization/covise)
- [HayStack](https://github.com/ingowald/hayStack)
- [OVITO](https://www.ovito.org/)
- [OpenUSD](https://openusd.org/release/index.html) (via [hdAnari](src/hdanari) plugin found in the ANARI-SDK)
- [pynari](https://github.com/ingowald/pynari) (Python language bindings)
- [TSD](https://github.com/NVIDIA/VisRTX/tree/next_release/tsd)
- [Vistle](https://vistle.io/)
- [VTK](https://vtk.org/)
- [Viskores](https://github.com/Viskores/viskores) (formerly known as VTK-m)
- [ANARI-Java](https://bitbucket.org/Eclesia/anari-java/src/main/) (Java language bindings)

### Integrations in-progress

- [Ascent](https://ascent.readthedocs.io/en/latest/)
- [ParaView](https://www.paraview.org/)
- [VisIt](https://visit-dav.github.io/visit-website/index.html)
- [VMD](https://www.ks.uiuc.edu/Research/vmd/)

If you integrate ANARI into your application, please open a PR to add it to this
list!
