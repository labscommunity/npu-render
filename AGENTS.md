# Repository instructions

- Do not change the repository's visibility, and do not publish packages or artifacts,
  without an explicit instruction from the owner.
- Every commit MUST follow Conventional Commits 1.0.0. Use the configured types
  and a meaningful lowercase description. Examples: `feat(runtime): add damage
  tiles`, `fix(openvino): preserve output ownership`, `docs: record hardware limits`.
- Do not bypass the commit hooks or CI. Merge, revert, and release commits are
  also subject to the policy. Configure hooks with `npm run hooks:install`.
- Preserve the long-term goal: one user-facing switch at supported graphics
  integration boundaries, freeing GPU resources for other applications.
- Keep device-specific APIs in providers. Keep pixel/operation contracts and
  backend policy independent of compositor and vendor implementations.
- Do not hide device fallback or claim unsupported operations migrated. Preserve
  the active renderer if preparing a replacement fails.
- Never present CPU checks or graph-call latency as proof of NPU execution,
  display frame delivery, or improved Tahoma performance.
- Preserve captured research evidence. Add new reports rather than editing old
  measurements. Do not commit model weights, compiled device caches, credentials,
  private machine addresses, or virtual environments.
- For runtime changes run relevant tests, `uv run ruff check .`, and
  `uv run ruff format --check .`. Verify device changes on real hardware where
  available and record unavailable hardware explicitly.
