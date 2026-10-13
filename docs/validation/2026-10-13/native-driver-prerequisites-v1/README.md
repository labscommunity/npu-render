# Physical host prerequisite snapshot

The read-only snapshot at 06:33 UTC records the Intel Arc B390 as the sole present
Display-class device, GPU driver 32.0.101.8724, and NPU driver 32.0.100.5540.
Windows still reports no interactive user. It records no installation, restart,
certificate-store change or security-setting change.

`Win32_DeviceGuard` reports VBS status 2 and running services including 2, so
VBS and memory integrity are running. Microsoft documents these values in its
[memory-integrity status reference](https://learn.microsoft.com/en-us/windows/security/hardware-security/enable-virtualization-based-protection-of-code-integrity?tabs=intune).
Compatibility with these protections must be qualified on the physical path;
the existing VM results alone do not establish it. Microsoft recommends
[functional testing with memory integrity enabled](https://learn.microsoft.com/en-us/windows-hardware/test/hlk/testref/driver-compatibility-with-device-guard).

The laboratory signing certificate is present in the machine's personal store,
but absent from its Root and TrustedPublisher stores. No private-key material
was exported. This absence alone does not prove test-signed kernel loading would
fail: Microsoft's [test-signing documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option)
distinguishes test-mode image signatures from ordinary root trust and requires
signed images with memory integrity. Package installation and actual loading
remain untested. The earlier read-only boot snapshot reported test-signing enabled.

Separately, the current laboratory UMD explicitly rejects adapter opening outside
the owned VM, and the external-worker transport has its own guest admission check.
The existing package is therefore not an implemented physical desktop switch.
See the [physical integration plan](../../../research/physical-desktop-presentation.md).

The raw snapshot was checked against the remote SHA-256 receipt before newline
normalization. The collector and original hash/size receipt are retained here.
