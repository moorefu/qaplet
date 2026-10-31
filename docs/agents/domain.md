# Domain Docs

Layout: single-context. Domain vocabulary lives in `GLOSSARY.md` at the repo root, and architecture decisions live in `docs/adr/`.

## Before exploring, read these

- Read `GLOSSARY.md` at the repo root.
- Read ADRs in `docs/adr/` that touch the area you are about to work in.

If these files do not exist, proceed silently. Do not flag their absence or suggest creating them upfront. The `/domain-modeling` skill, also reached through `/grill-with-docs` and `/improve-codebase-architecture`, creates them lazily when terms or decisions get resolved.

## Use the glossary's vocabulary

When naming a domain concept in an issue title, refactor proposal, hypothesis, or test name, use the term defined in `GLOSSARY.md`. Avoid synonyms the glossary explicitly excludes.

If a concept is missing, reconsider whether the project uses it or note the gap for `/domain-modeling`.

## Flag ADR conflicts

If a proposal contradicts an existing ADR, identify that ADR explicitly and explain why the decision should be revisited.
