## Agent skills

### Issue tracker

Issues and specs are tracked as local Markdown files under `.scratch/`. See `docs/agents/issue-tracker.md`.

### Triage labels

Use the five default triage labels. See `docs/agents/triage-labels.md`.

### Domain docs

Use a single-context layout: root `GLOSSARY.md` and `docs/adr/`. See `docs/agents/domain.md`.

### Local validation

Build artifacts and SDK distributions on Ubuntu; other platforms validate supplied artifacts and SDKs. Keep reusable inputs and build caches under the repository-root `.cache/`.

Before environment diagnosis, compiler selection, cache layout changes, or CI changes, read `docs/development.md` for execution-context evidence, the local check entry point, and CI failure triage.
