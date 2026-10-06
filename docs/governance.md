# Repository governance

The repository must remain private. Every commit must follow Conventional Commits
1.0.0, including roots, reverts, and releases. The configured commitlint rules add
a 100-character subject limit and a conventional type vocabulary.

## Active controls

- `AGENTS.md` and `CONTRIBUTING.md` mandate the policy.
- `.githooks/commit-msg` checks each local commit message.
- `.githooks/pre-push` checks the entire ancestry of each pushed tip.
- CI checks the complete checked-out ancestry and PR title; default message
  exemptions are disabled.
- GitHub merge settings allow squash merges only, using the PR title as the
  resulting commit subject.
- Dependencies are locked. GitHub Actions use pinned commit references.
- No package publication or public deployment workflow exists.

Git does not install hooks in fresh clones. Run `npm ci` and
`npm run hooks:install` after cloning.

## Server enforcement limitation

At setup, GitHub returned HTTP 403 when asked to activate a commit-message ruleset:
the account must upgrade to GitHub Pro to enable that feature for this private
repository. The repository remains private. Local hooks and CI are active, but
they do not substitute for a server rejecting noncompliant direct pushes.

The intended all-branch rule is checked in as
[conventional-commits-ruleset.json](../.github/conventional-commits-ruleset.json).
After the account supports private-repository rulesets, activate it with:

```sh
gh api --method POST repos/t8/npu-render/rulesets --input .github/conventional-commits-ruleset.json
```

The intended main-branch protection configuration requires CI, pull requests,
linear history, and resolved conversations, and disallows forced pushes/deletion.
It is checked in as [main-protection.json](../.github/main-protection.json).

```sh
gh api --method PUT repos/t8/npu-render/branches/main/protection --input .github/main-protection.json
```

Re-read both API responses and the effective settings after activation. Do not
describe checked-in policy files as active server enforcement until confirmed.
