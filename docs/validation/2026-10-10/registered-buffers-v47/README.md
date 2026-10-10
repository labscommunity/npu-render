# Registered constant-buffer validation

The installed Windows driver passes 20 exact readbacks across five buffer sizes,
including initialization, updates and write-discard maps. No allocations or
contexts remain alive; no rejected calls or cleanup errors are recorded.

This validates CPU-backed VidMm resource handling, with no NPU draw. Desktop
activation remains unsuccessful. [report.json](report.json) preserves both
earlier failures, the corrected probe and the separate activation result.
