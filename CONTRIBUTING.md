# Contributing

The repository is private until its owner publishes it. Keep work and artifacts within
authorized destinations, and never commit model weights, compiled device caches, the SHAVE
toolchain, credentials, private machine addresses or virtual environments.

## Setup

Install Python 3.11+, uv, and Node.js 22+ with npm, then run:

```sh
uv sync --frozen
npm ci
npm run hooks:install
```

Git does not install hooks when cloning. The final command sets this checkout's
`core.hooksPath` to `.githooks`. The commit hook validates the complete message;
the push hook checks every ancestor of each pushed commit, including its root.

## Mandatory commit format

Use [Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/):

```text
feat(runtime): add a prepared backend switch
fix(buffers): retain ownership of returned pixels
docs: record a failed hardware qualification
feat(api)!: require explicit color-space metadata
```

Allowed types: `build`, `chore`, `ci`, `docs`, `feat`, `fix`, `perf`, `refactor`,
`revert`, `style`, and `test`. Headers are at most 100 characters. Separate the body
and footers from the subject by a blank line. Use `!` or a `BREAKING CHANGE:` footer
for breaking changes. Default merge/revert/release-message exemptions are disabled.

CI validates every commit in the checked-out branch's ancestry and the PR title.
Repository merge settings use squash commits with the PR title as the subject.
Use a conventional PR title; do not use GitHub's default `Merge ...` message.
No `--no-verify` bypasses. Fix a rejected message before pushing.

GitHub rejected server-side rules on the current account plan. The policy is
mandatory, and hooks/CI check it, but GitHub itself cannot currently reject a
noncompliant direct push to this private repository. See
[governance status and activation instructions](docs/governance.md).

## Validation

```sh
uv run pytest
uv run ruff check .
uv run ruff format --check .
npm run lint:commits
```

CPU CI checks semantic correctness, output ownership, failed-switch behavior,
selection persistence, and device-report validation. Hardware qualification is
separate: run `tools/validate_switch.py` with explicit target devices and save its
JSON report. Do not enable fallback to make a hardware check pass.

The initial numeric tolerances are experimental. New operations need independent
oracles and a declared format, precision, synchronization, and deadline contract.
Keep compilation and qualification outside frame execution.
