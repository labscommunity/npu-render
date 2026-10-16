# General four-sample path in the native worker

The native worker now admits all 24 observed uses of the four-sample shader.
The selected replay passes all 14 requests with ten admissions. The complete
replay passes all 972 requests with 24 admissions, one provider generation and
zero provider faults. Every one of the 971 available original pixel references
matches. Request 971 has no original response reference.

A separate read-only comparison against the previous qualified worker finds all
972 output hashes and byte lengths identical, including request 971. This is a
cross-replay consistency check, not a substitute for an original reference where
one is absent. It also corrects the earlier live-integration README's mistaken
statement that 972 original references existed; the original raw reports were
already explicit about 971 matches and one unreferenced success.

The first selected replay does not start its worker because the expected derived
build receipt is missing. That failure is preserved. A receipt derived from the
pinned clean-build result supplies the required schema without changing the
binary. The subsequent selected and complete replays pass. Both source and
binary hashes, source snapshots, logs, receipts and replay comparisons are retained.

These results qualify broader integration in the named-pipe worker. They do not
qualify a live desktop run with this revised worker. The last successful live
trial still uses the earlier three-admission worker. No new driver installation,
desktop topology change or Tahoma trial occurs here. Whole-desktop responsiveness,
ordinary application support, matched GPU/Tahoma measurements and a usable switch
remain unfinished.
