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

The [session admission experiments](../validation/2026-10-13/native-session-progress-v1/)
now qualify the global per-consumer record in an owned process, including ten
NPU drawing operations. They also expose a concrete access gap: read-only checks
using the actual DWM and Local Service driver-host tokens deny both the current
worker pipe and an owned probe process's query/synchronization permissions.
Explicit identity entries pass the DACL calculations. Live cross-account calls,
actual cross-session execution and native indirect-display bootstrap remain
unqualified; preparing an ACL is not compositor execution.

The [Local Service account trial](../validation/2026-10-13/native-service-progress-v1/)
then reproduces denial with the original process permissions and passes ten real
NPU operations after adding query/synchronization access to the owned controller
and worker. Exact original DACLs and temporary tasks/files are restored. This
qualifies an owned diagnostic under the driver host's account; actual WUDFHost
bootstrap and DWM execution remain separate work.

The [native routing candidate](../validation/2026-10-13/native-routing-progress-v1/)
now builds separate compositor/indirect-host admission and a native indirect
variant with a bounded ready-event wait. Both host identities can be pinned in
the worker, and Local Service can query the console session state. Both signed
packages [stage and remove cleanly](../validation/2026-10-13/native-pair-stage-v1/).
Actual native driver-host entry, lease publication during installation and DWM
adapter selection remain untested. The physical probe's console checks reject
before showing a window when no signed-in unlocked desktop is available.

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

The [native desktop controller candidate](../validation/2026-10-13/native-desktop-progress-v1/)
connects the two packages, pins the compositor and newly created indirect host,
publishes separate leases, and records native frame/NPU evidence. Its supporting
registered D3D11, simultaneous-consumer, identity-observer, and actual timed
recovery trials pass. The launcher rejects unavailable interactive desktops;
its actual desktop installation branch has not run. A finite watcher was armed
on October 13, 2026, to attempt the next tests after the owner signs in and unlocks.
Its archived waiting snapshot is not an ongoing execution claim. Physical frame
delivery, primary-display migration, input mapping, topology restoration, and
matched Tahoma measurements remain outstanding.

The later [native desktop trials](../validation/2026-10-13/native-desktop-live-v1/)
run on the unlocked console and establish actual WUDFHost bootstrap, requested
adapter selection and DWM NPU execution. They also expose native desktop failures:
the first large untextured draw exceeds the transport limit; its repaired path
works in the second trial, which rejects a subsequent textured draw. DWM exits
before frame readback in both trials and recovery succeeds. The separate physical
probe now submits a verified static frame, but lost ETW events prevent confirming
independent flip. These results supersede the earlier waiting snapshots above;
they do not establish a usable desktop or a Tahoma benefit.
