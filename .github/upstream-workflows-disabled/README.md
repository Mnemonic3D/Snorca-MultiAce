# Upstream workflows (switched off)

These are FOrcaSlicer's original GitHub Actions files, kept unchanged for reference.
GitHub only runs files inside `.github/workflows/`, so nothing in this folder runs.
`sync-to-gitea.yml` in particular pushes to the upstream author's own mirror and must never be enabled here.
The Snorca MultiAce builds are in `.github/workflows/` and start only when "Run workflow" is pressed.
- `dependabot.yml` (automatic dependency-update pull requests) is also parked here so nothing starts by itself.
