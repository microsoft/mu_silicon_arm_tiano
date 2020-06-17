=======================================
Project Mu Silicon Arm Tiano Repository
=======================================

.. |build_status_windows| image:: https://dev.azure.com/projectmu/mu/_apis/build/status/mu_silicon_arm_tiano%20PR%20gate?branchName=release/202005

|build_status_windows| Current build status for release/202005

This repository is part of Project Mu.  Please see Project Mu for details https://microsoft.github.io/mu

Branch Status - release/202005
==============================

Status:
  In Development

Entered Development:
  2020/06/16

Anticipated Stabilization:
  August 2020

Branch Changes - release/202005
===============================

Breaking Changes-dev
--------------------

- None

Main Changes-dev
----------------

- None

Bug Fixes-dev
-------------

- [TCBZ2574] Add missing components to ArmPkg.dsc
- [TCBZ2575] Add missing components to ArmPlatformPkg.dsc

2005_RefBoot Changes
--------------------

- Incomplete

2005_CIBuild Changes
--------------------

- Incomplete

2005_Rebase Changes
-------------------

Source Commit from dev/202005: 7f1a05f25cf

- ArmPkg/PlatformBootManagerLib: connect non-discoverable USB hosts
  - Non-discoverable USB host controllers will no have a PCI I/O protocol installed on their handle
  - Those protocols will be connected to those handles explicitly now  so that platforms that do
    not rely on ConnectAll () continue working as expected

- ArmPkg/PlatformBootManagerLib: register 's' as UEFI Shell hotkey
  - Since the UEFI shell will be hidden as an ordinary boot option, 's' is registered as a shell hotkey
  - If a user goes through the UiApp, ConnectAll () will be called

- ArmPkg/PlatformBootManagerLib: fall back to the UiApp on boot failure
  - If no active boot options are successful, the platform will no drop into the UiApp application
    - This will execute ConnectAll () and allow the user to use the Boot Manager to select a network or
      removable disk option

- ArmPkg/PlatformBootManagerLib: hide UEFI Shell as a regular boot option
  - Since ConnectAll () is no longer called on every boot, block devices and other devices will not
    automatically be connected
  - Therefore the shell might be confusing to novice users so it is now hidden by default

- ArmPkg/PlatformBootManagerLib: Don't connect all devices on each boot
  - EfiBootManagerConnectAll () is not called on every boot path
  - EfiBootManagerConnectAll () will still be called from the UiApp
  - Boot#### variables will also only be created in the UiApp now

Code of Conduct
===============

This project has adopted the Microsoft Open Source Code of Conduct https://opensource.microsoft.com/codeofconduct/

For more information see the Code of Conduct FAQ https://opensource.microsoft.com/codeofconduct/faq/
or contact `opencode@microsoft.com <mailto:opencode@microsoft.com>`_. with any additional questions or comments.

Contributions
=============

Contributions are always welcome and encouraged!
Please open any issues in the Project Mu GitHub tracker and read https://microsoft.github.io/mu/How/contributing/


Copyright & License
===================

Copyright (c) Microsoft Corporation. All rights reserved.
SPDX-License-Identifier: BSD-2-Clause-Patent

Upstream License (TianoCore)
============================

Copyright (c) 2019, TianoCore and contributors.  All rights reserved.

SPDX-License-Identifier: BSD-2-Clause-Patent

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

Subject to the terms and conditions of this license, each copyright holder
and contributor hereby grants to those receiving rights under this license
a perpetual, worldwide, non-exclusive, no-charge, royalty-free, irrevocable
(except for failure to satisfy the conditions of this license) patent
license to make, have made, use, offer to sell, sell, import, and otherwise
transfer this software, where such license applies only to those patent
claims, already acquired or hereafter acquired, licensable by such copyright
holder or contributor that are necessarily infringed by:

(a) their Contribution(s) (the licensed copyrights of copyright holders and
    non-copyrightable additions of contributors, in source or binary form)
    alone; or

(b) combination of their Contribution(s) with the work of authorship to
    which such Contribution(s) was added by such copyright holder or
    contributor, if, at the time the Contribution is added, such addition
    causes such combination to be necessarily infringed. The patent license
    shall not apply to any other combinations which include the
    Contribution.

Except as expressly stated above, no rights or licenses from any copyright
holder or contributor is granted under this license, whether expressly, by
implication, estoppel or otherwise.

DISCLAIMER

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
