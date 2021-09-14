# HEST & SDEI

Hardware Error Source Table HEST [1] and Software Delegated Exception Interface
SDEI [2] ACPI tables are used to accomplish firmware first error handling.This
patch series introduces a framework to build and install the HEST ACPI table
dynamically.

The following figure illustrates the possible usage of the dynamic
generation of HEST ACPI table.

``` ascii art
                                    NS | S
+--------------------------------------+--------------------------------------+
|                                      |                                      |
|+-------------------------------------+---------------------+                |
||               +---------------------+--------------------+|                |
||               |                     |                    ||                |
|| +-----------+ |+------------------+ | +-----------------+|| +-------------+|
|| |HestTable  | ||  HestErrorSource | | | HestErrorSource ||| | DMC-620     ||
|| |  DXE      | ||        DXE       | | |  StandaloneMM   ||| |Standalone MM||
|| +-----------+ |+------------------+ | +-----------------+|| +-------------+|
||               |GHESv2               |                    ||                |
||               +---------------------+--------------------+|                |
||          +--------------------+     |                     |                |
||          |PlatformErrorHandler|     |                     |                |
||          |        DXE         |     |                     |                |
||          +--------------------+     |                     |                |
||FF FWK                               |                     |                |
|+-------------------------------------+---------------------+                |
|                                      |                                      |
+--------------------------------------+--------------------------------------+
                                       |
                   Figure: Dynamic Hest Table Generation.
```

All the hardware error sources are added to HEST table as GHESv2[3] error source
descriptors. The framework comprises of following DXE and MM drivers:

- HestTableDxe:
  Builds HEST table header and allows appending error source descriptors to the
  HEST table. Also provides protocol interface to install the built HEST table.

- HestErrorSourceDxe & HestErrorSourceStandaloneMM:
  These two drivers together retrieve all possible error source descriptors of
  type GHESv2 from the MM drivers implementing HEST Error Source Descriptor
  protocol. Once all the descriptors are collected HestErrorSourceDxe appends
  it to HEST table using HestTableDxe driver.

- PlatformErrorHandlerDxe:
  Builds and installs SDEI ACPI table. This driver does not initialize(load)
  until HestErrorSourceDxe driver has finished appending all possible GHESv2
  error source descriptors to the HEST table. Once that is complete using the
  HestTableDxe driver it installs the HEST table.

This patch series provides reference implementation for DMC-620 Dynamic Memory
Controller[4] that has RAS feature enabled. This is platform code
implemented as Standalone MM driver in edk2-platforms.

## References

[1]: ACPI 6.3, Table 18-382, Hardware Error Source Table
[2] : SDEI Platform Design Document, revision b, 10 Appendix C, ACPI table definitions for SDEI
[3] : ACPI Reference Specification 6.3, Table 18-393 GHESv2 Structure
[4] : DMC620 Dynamic Memory Controller, revision r1p0
[5] : UEFI Reference Specification 2.8, Appendix N - Common Platform Error
      Record
[6] : UEFI Reference Specification 2.8, Section N.2.5 Memory Error Section

Link to github branch with the patches in this series -
<https://github.com/omkkul01/edk2/tree/ras_firmware_first_edk2>
