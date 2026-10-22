# Repository governance

The repository is public at `labscommunity/npu-render` under the Apache License
2.0 (transferred from `t8/npu-render` and published on 2026-10-22). Every commit
must follow Conventional Commits 1.0.0, including roots, reverts, and releases.
The configured commitlint rules add a 100-character subject limit and a
conventional type vocabulary.

## Active controls

- `AGENTS.md` and `CONTRIBUTING.md` mandate the policy.
- `.githooks/commit-msg` checks each local commit message.
- `.githooks/pre-push` checks the entire ancestry of each pushed tip.
- CI checks the complete checked-out ancestry and PR title; default message
  exemptions are disabled.
- GitHub merge settings allow squash merges only, using the PR title as the
  resulting commit subject.
- Dependencies are locked. GitHub Actions use pinned commit references.
- No package publication or deployment workflow exists.

Git does not install hooks in fresh clones. Run `npm ci` and
`npm run hooks:install` after cloning.

## Server enforcement

Activated on 2026-10-22, once the repository was public, and confirmed by
reading the effective settings back:

- Ruleset 25669870, "Conventional Commits on every branch" (checked in as
  [conventional-commits-ruleset.json](../.github/conventional-commits-ruleset.json)):
  active on every branch, no bypass actors; GitHub rejects a push whose commit
  subject does not match the conventional header pattern.
- Main-branch protection (checked in as
  [main-protection.json](../.github/main-protection.json)): the four CI checks
  are required and the branch must be up to date with `main` before merging,
  pull requests are required (zero approvals), conversations must be resolved,
  history must be linear, force pushes and deletion are disallowed, and the
  rules apply to administrators.

Both were applied with the checked-in files:

```sh
gh api --method POST repos/labscommunity/npu-render/rulesets --input .github/conventional-commits-ruleset.json
gh api --method PUT repos/labscommunity/npu-render/branches/main/protection --input .github/main-protection.json
```

Before publication the account's plan did not allow either on a private
repository (GitHub answered HTTP 403), so until 2026-10-22 only the local
hooks and CI checked the policy. Re-read the effective settings after any
change to the checked-in files; do not describe them as active server
enforcement until confirmed.
