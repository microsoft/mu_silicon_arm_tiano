=======================================
Project Mu Silicon Arm Tiano Repository
=======================================

============================= ================= =============== ===================
 Host Type & Toolchain        Build Status      Test Status     Code Coverage
============================= ================= =============== ===================
Windows_VS2022_               |WindowsCiBuild|  |WindowsCiTest| |WindowsCiCoverage|
Ubuntu_GCC5_                  |UbuntuCiBuild|   |UbuntuCiTest|  |UbuntuCiCoverage|
============================= ================= =============== ===================

This repository is part of Project Mu.  Please see Project Mu for details https://microsoft.github.io/mu

Branch Status - release/202502
==============================

:Status:git
  In Development

:Entered Development:
  2025/02/21 (Date Edk2 started accepting changes which were not in a previous release)

:Anticipated Stabilization:
  May 2025

Branch Changes - release/202502
===============================

Breaking Changes-dev
--------------------
- ArmPkg: ArmGicLib (SEC version) has been dropped.
- ArmPkg: ArmPkg\Drivers\ArmGic\ArmGicDxeLib.inf has been moved to ArmPkg\Drivers\ArmGicDxe\ArmGicDxe.inf
- ArmPkg: Include files in ArmPkg\Include\Chipset has been moved into Mdepkg\Include and refactored.
- ArmPkg: ArmDisassemblerLib has been dropped.
- ArmPkg: ArmGicArchLib has been refactored and dropped.
- ArmPkg: ArmLib.h has been moved to MdePkg.
- ArmPkg: AsmMacroIoLibV8.h has been renamed and moved into MdePkg under Aarch64/AsmMacroLib.h.
- ArmPkg: ArmGicArchLib has been refactored and dropped.
- ArmPkg: ArmGicArchSecLib has been refactored and dropped.
- ArmPkg: ArmSmcPsciResetSystemLib has been refactored and dropped. Use ArmPsciResetSystemLib instead.
- ArmPkg: ArmSoftFloatLib has been dropped.
- ArmPlatformPkg: ArmPlatformStackLib has been dropped.
- ArmPlatformPkg: PrePeiCoreMPCore and PrePeiCoreUniCore have been refactored and dropped. Use ArmPlatformPkg/Sec/Sec.inf
- ArmPlatformPkg: PeiMPCore.inf and PeiUniCore.inf have been refactored and dropped. Use ArmPlatformPkg/PeilessSec/PeilessSec.inf

MU Overrides on EDK2
--------------------
- At the end of 202405, mu_silicon_arm_tiano contained 135 commits on top of edk2-stable202405.
- At the start of 202502, mu_silicon_arm_tiano contains 33 commits on top of edk2-stable202502.
Full MU changes list can be viewed `in the changelog <https://github.com/microsoft/mu_silicon_arm_tiano/compare/fbe0805b2091393406952e84724188f8c1941837...dev/202502>`_.

Platform Integration Reference
------------------------------
Reference platforms which consume release/202502 are available in `mu_tiano_platforms <https://github.com/microsoft/mu_tiano_platforms>`_.

Please note that this version of EDK2 has specific requirements when it comes to TF-A support. 
Platforms that consume this version of EDK2 must ensure their TF-A `contains this set of patches <https://review.trustedfirmware.org/q/topic:%22hob_creation_in_tf_a%22>`_.
Failure to contain the appropriate patches will result in a failure to boot.

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

| Copyright (C) Microsoft Corporation
| SPDX-License-Identifier: BSD-2-Clause-Patent

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

.. ===================================================================
.. This is a bunch of directives to make the README file more readable
.. ===================================================================

.. CoreCI

.. _Windows_VS2022: https://dev.azure.com/projectmu/mu/_build/latest?definitionId=51&&branchName=release%2F202502
.. |WindowsCiBuild| image:: https://dev.azure.com/projectmu/mu/_apis/build/status%2FCI%2FMu%20Silicon%20Arm%20Tiano%20CI%20VS?repoName=microsoft%2Fmu_silicon_arm_tiano&branchName=release%2F202502
.. |WindowsCiTest| image:: https://img.shields.io/azure-devops/tests/projectmu/mu/51.svg
.. |WindowsCiCoverage| image:: https://img.shields.io/badge/coverage-coming_soon-blue

.. _Ubuntu_GCC5: https://dev.azure.com/projectmu/mu/_build/latest?definitionId=52&&branchName=release%2F202502
.. |UbuntuCiBuild| image:: https://dev.azure.com/projectmu/mu/_apis/build/status%2FCI%2FMu%20Silicon%20Arm%20Tiano%20CI%20Ubuntu%20GCC5?repoName=microsoft%2Fmu_silicon_arm_tiano&branchName=release%2F202502
.. |UbuntuCiTest| image:: https://img.shields.io/azure-devops/tests/projectmu/mu/52.svg
.. |UbuntuCiCoverage| image:: https://img.shields.io/badge/coverage-coming_soon-blue
