# Tactical AI Lab

Native Unreal tactical-AI tooling for scenario authoring, deterministic simulation, map baking, GOAP debugging, EQS evaluation, and lifetime analysis.

## Features

- Tactical scenario asset type and editor toolkit
- Deterministic tactical simulation
- Built-in enemy simulation profiles
- Scenario load/save support
- Lifetime, batch-summary, and failure-report output
- Goal graph and tactical surface editor views
- EQS voxel-node generation
- Cover line-of-sight testing
- Voxel-path testing
- PIE debug integration
- Runtime tactical evaluator
- Integration points for GOAP and traversal navigation

## Dependencies

This plugin depends on:

- `HellRunTraversalNavigation`
- `HellRunGOAP`

## Modules

- `HellRunTacticalLab` — Runtime
- `HellRunTacticalLabEditor` — Editor

## Documentation

See [Docs/ArchitectureAudit.md](Docs/ArchitectureAudit.md) for the architecture audit included with the repository.

## Basic setup

1. Install and enable **HellRunTraversalNavigation** and **HellRunGOAP**.
2. Copy this plugin into your project's `Plugins` directory.
3. Enable **Tactical AI Lab**.
4. Create a Tactical Lab Scenario asset and open it with the custom editor.

## Status

Version 0.1.0. The plugin descriptor currently marks the project as beta.
