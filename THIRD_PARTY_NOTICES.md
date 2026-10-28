# Third-party notices

## OpenVR SDK headers

`vendor/openvr_capi_102.h` is from Valve's OpenVR SDK 1.0.2
(https://github.com/ValveSoftware/openvr). `vendor/openvr_compat.h` is
generated from it. Licence:

```
Copyright (c) 2015, Valve Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Leia SR (rez3d_leia_sr.dll only)

The optional `rez3d_leia_sr.dll` is built against the Leia SR SDK 1.34.10
through SR-lib. Neither is included in this repository, and the SR runtime DLLs
are never shipped: they come with the user's SR Platform.

- Leia SR SDK: Copyright 2025, Leia, Inc. The SDK's permission notice and the
  third-party licences it lists are in `licenses/LeiaSR-SDK-License.txt`.
- SR-lib: Copyright (c) Bo3b Johnson, MIT licence
  (https://github.com/bo3b/SR-lib).

The approach follows oneup03's RT64 3D work and bo3b's SR-lib.

## Trademarks

Rez and Rez Infinite are trademarks of their respective owners (Enhance, Inc.).
Leia, Simulated Reality and SR are trademarks of Leia, Inc.
This project is an unofficial, fan-made add-on. It is not affiliated with or
endorsed by Enhance, Monstars or Resonair, and it contains no game code or assets.
