# Automatic admission through the registered Direct3D driver

Registration v32 passes with candidate package v18. A normally started Direct3D
fixture creates a device on the registered NPU adapter, obtains a per-process
lease through the UMD's discovery handshake, and completes eight checked NPU
operations: six clears and two draws. No suspended-process startup, pre-created
consumer PID lease or explicit handshake call is added to the application.
The application explicitly chooses the registered adapter; automatic default
adapter selection in an interactive session remains a separate gate.

The broker verifies the actual pipe client PID, process creation time, session
and SID. Its one-consumer lease and discovery publication are removed after the
application exits. The hardware worker records all eight operations and delivery
acknowledgments, with one isolated provider generation. The test restores ordinary
adapter state with memory integrity enabled. It runs in the controller's session
and does not change the live desktop or prove browser/video/game compatibility.

The UMD changes its initial device admission gate to discover a broker only when
no per-process lease exists. Existing inaccessible or malformed leases are not
replaced. The subsequent transport and lease checks remain in place. The clean
build passes existing lease, unleased-device and trace-lifecycle contracts. The
finite broker independently passes a normal-start, dummy-worker component check.

The first registration attempt, v31, failed before rendering because package v18
had no completed staging report. Its cleanup passed. The second coordinator
performs staging and recovery before registration v32. Both attempts are retained.

Key binary identities are:

| Artifact | SHA-256 |
| --- | --- |
| Unsigned UMD | `a03c9b0ebc248eea2e8684ccb23fc02d9f322cce0ace182c4e5d5a23336c4e2e` |
| Signed installed UMD | `2aaccb7ca53c8b36980b8cd2b132c2b8d94b1b19aef4ca4a8d944708287c042d` |
| Admission broker | `63396fcda5330b0590b038c8bd9d6ab8a31f0ee2887716fc4f4ec1af1f89ea6f` |

Cross-session interactive applications, standard-user and sandbox admission,
renewal/revocation, shared live-desktop scheduling, default-device selection and
preserving Tahoma's explicit GPU choice remain unqualified. This is a bounded
registered graphics test, not full desktop offload or a usable switch.
