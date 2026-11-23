# NanoVDB headers

`nanovdb/NanoVDB.h` and the two headers it includes, `nanovdb/util/Util.h` and
`nanovdb/math/Math.h`, unmodified from [OpenVDB](https://github.com/AcademySoftwareFoundation/openvdb)
13.0.0 (NanoVDB 32.9.0, the version VisRTX vendors). They are licensed under
Apache-2.0 (`LICENSE`), copyright Contributors to the OpenVDB Project.

The CTS uses them to build the NanoVDB grids of its `nanovdb` spatial field
Tests in code (`src/anari_test_scenes/cts/tests/NanoVdbGrid.cpp`), so no grid
file is checked in. Nothing else in the SDK includes them. NanoVDB.h is no
longer a single header since NanoVDB 32.7; these three are all it needs.
