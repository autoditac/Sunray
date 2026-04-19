# Changelog

All notable changes to the Alfred fork of Sunray are tracked here.

The format is loosely based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Fork versions follow the pattern `<upstream-version>-autoditac.<n>` (e.g. `1.0.331-autoditac.1`), where the upstream version is the last `Ardumower/Sunray` tag that was merged and `<n>` counts the fork release on top of it.

Only changes that diverge from `Ardumower/Sunray` (the upstream default branch `master`) are listed. See the upstream repo for Sunray's own release notes.

## [Unreleased]

Branch: `phase-1-steer-logging` (PR [#2](https://github.com/autoditac/Sunray/pull/2)).

### Added

- **Phase-1 STEER logging** for steering-behaviour analysis. `Motor::setLinearAngularSpeed()` emits a `STEER:` line every 100 ms while the wheels are commanded above 0.5 RPM, capturing commanded vs. measured wheel RPM, PWM, motor current, Stanley lateral error, and three yaw-rate estimates (fused, IMU-only, encoder-only). The `(wEnc - wImu)` delta directly distinguishes dead-zone stall, load stall, and traction slip. Guarded by `#define STEER_LOG` in `configs/config.h` and `linux/config_alfred.h`. (`89eb6eb`)
- **Chassis/hardware documentation** in `README.md`: identifies Güde GRR 240.1 as the donor chassis and lists spare-part numbers, prices, and source URLs for all wearing components. (`816abaa`, `98d449f`, `9cbf9dc`)
- **Steering analysis document** `doc/steering-analysis-2026-04.md`: pipeline walk-through, chassis mechanics, ADR-004 audit, proposed geometric-envelope replacement for `MIN_WHEEL_SPEED`, debugging plan, and roll-out strategy. Includes H1 (dead-zone) vs H2 (traction-slip) hypothesis discrimination and the 43.75 % rear-weight measurement. (`f7c6483`, `c116d53`, `e147d4d`, `41feb9c`, `00490df`, `30d71c7`, `31135ed`)

## [1.0.331-autoditac.1] — 2026-04 (last tagged fork release)

Synced on top of upstream `1.0.331`.

### Added

- Ansible agent workflow for mower deployment (`7c61442`).
- Caster-bearing rust issue note and igus replacement reference in README (`374a6e5`).

### Changed

- CI: `:latest` tag is pushed only on release; pushes to `main` produce SHA-only `:alpha` images that auto-deploy to batman (`bad9596`, `8b7ac67`).
- README: documents safe auto-update policy and release-only `:latest` tagging (`bad9596`).
- README fork-diff table: records last upstream merge SHA for traceability (`f19e38c`).

### Fixed

- Five upstream reliability bugs for Alfred, including the `SerialRobotDriver` L/R encoder + PWM swap (upstream [#151](https://github.com/Ardumower/Sunray/issues/151)). This is a hard prerequisite for any per-wheel software steering fix. (`feac90b`)

---

## Fork policy

- **Upstream sync:** the fork tracks `Ardumower/Sunray` `master` via periodic merges. The last merged upstream SHA is recorded in `README.md`.
- **Deployment:** pushes to `main` build Docker images tagged `:alpha` and `:sha-<commit>` and deploy automatically to `batman` (development mower). Releases with a `vX.Y.Z` or `X.Y.Z-autoditac.N` tag additionally publish `:latest` for the production fleet.
- **One PR per feature.** Fork-specific features and fixes live on topic branches (`phase-1-steer-logging`, `feat/*`, `fix/*`) and land through pull requests on `autoditac/Sunray`.

[Unreleased]: https://github.com/autoditac/Sunray/compare/1.0.331-autoditac.1...main
[1.0.331-autoditac.1]: https://github.com/autoditac/Sunray/releases/tag/1.0.331-autoditac.1
