# Connecting the NPU desktop renderer to the physical screen

The v120 trial renders a Windows VM's desktop through Delta's NPU and delivers
the resulting frames to a Mac viewer. Delta's physical desktop still uses its
ordinary graphics path. Replacing that viewer with a native Windows presentation
bridge is an integration candidate, not an achieved desktop switch.

An indirect display can request the NPU-backed render adapter. Windows reports
the adapter actually selected, which must be checked rather than assumed.
This preference applies to the indirect adapter's swapchains; it does not select
the renderer for an unrelated physical monitor.
[Microsoft's adapter-selection contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/nf-iddcx-iddcxadaptersetrenderadapter)

A proposed physical path is:

1. Place the interactive desktop and participating applications on an indirect
   primary monitor rendered by the registered NPU-backed adapter.
2. Present the completed NPU frames through a borderless flip-model window on the
   physical monitor. Preserve the physical display driver and its scanout hardware.
3. Verify independent flip or hardware overlay operation and measure GPU work.
   Microsoft documents configurations in which flip presentation bypasses desktop
   composition and allows DWM to sleep; other visible content can reintroduce
   composition. A fullscreen window or successful `Present` call alone is not
   evidence that this mode engaged.
   [DXGI presentation guidance](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model)
4. Preserve keyboard focus in the original applications, map pointer input to the
   indirect desktop, and restore the exact prior topology on exit, failure or a
   watchdog timeout. Validate cursor behavior and separate secure-desktop transitions.

This is an inference from the documented interfaces. Native adapter admission,
physical frame delivery, input behavior, application placement, recovery and
resource relief all need separate experiments. A bridge may still use GPU copies
or composition. Those costs must be counted; a copied image must not be reported
as entirely eliminating GPU activity. The older capture overlay is not sufficient:
its original applications continue rendering through the existing GPU path.

The current laboratory UMD also deliberately rejects `OpenAdapter10` outside
the owned VM, and its external-worker transport has a separate guest-only
admission check. The existing signed laboratory package cannot simply be
installed as a physical desktop switch. Physical operation needs its own
qualified admission, local worker transport, installation and recovery path;
the guest guards remain the default. A physical presentation probe establishes
only the output boundary. A read-only prerequisite snapshot can record active
graphics drivers, code-integrity configuration and certificate trust while the
interactive desktop is unavailable.

The [native registered-adapter trial](../validation/2026-10-13/native-windows-adapter-v1/)
now qualifies a separate native UMD for one bounded controller-owned process:
six clears and two draws pass ordinary Direct3D 11 calls and real Windows callbacks
on Delta's NPU. The installed diagnostic and temporary certificate trust were
removed successfully with memory integrity still running. This closes the
single-process native loader/transport boundary. It does not admit DWM or select
that adapter for an indirect monitor.

The [session snapshot](../validation/2026-10-13/native-adapter-promoted-v1/)
confirms that DWM runs under a separate virtual account in console session 1,
while the remote diagnostic controller and worker used the services session.
The current lease requires all three processes in one session and binds one
consumer PID/creation time. Desktop integration therefore needs qualified
session/account admission, multiple participating consumers, worker access for
the compositor, and bounded revocation/recovery. At the snapshot time there was
no signed-in user in either a console or remote desktop session. Secure sign-in
must not be substituted for an interactive user desktop test.

The hybrid-GPU model does not solve DWM migration by itself. Microsoft's documented
redirected and direct-flip paths use the integrated GPU for DWM composition, even
when another adapter renders applications.
[Cross-adapter rendering](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/rendering-on-a-discrete-gpu-using-cross-adapter-resources)

The next bounded probe should accept an already verified NPU-produced frame,
perform only upload/copy and flip presentation, record adapter/output identities,
and collect PresentMon/ETW presentation modes. It must restore its owned window on
timeout and avoid changing display-driver or boot configuration. Native desktop
routing follows that probe and the renderer's performance work. Current 83–207
second VM interaction times do not qualify normal desktop use.

After actual native routing works, compare ordinary desktop operation with the
NPU mode under the same visual workload and Tahoma settings. Retain GPU graphics,
copy and compute activity, NPU activity, frame latency, inference throughput and
power measurements. Keeping scanout hardware active is compatible with freeing
GPU compute; the amount of useful capacity recovered remains a measured question.
