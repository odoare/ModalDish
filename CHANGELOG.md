# Changelog

All notable changes to ModalDish. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/).

To release: rename `[Unreleased]` to `[X.Y.Z] - YYYY-MM-DD`, start a new empty
`[Unreleased]` above it, commit, then tag `vX.Y.Z`. The version comes from the
tag alone (`cmake/Version.cmake`). The release workflow publishes that section
as the release notes, and stops before building if the tag has no section
here (`.github/changelog-notes.sh`).

## [Unreleased]

## [0.2.0] - 2026-09-04

### Added

- **ModalDish Instrument**, a second plugin: the same plate as an instrument,
  for MIDI and instrument tracks and for hosts that send no MIDI to effects
  (Ableton Live). Its audio input is off by default and offered by hosts as a
  sidechain. VST3, AU and CLAP (CLAP id `com.fx-mechanics.ModalDishInstrument`),
  in the same archives and installer as the effect. Presets are shared;
  ModalDish itself is unchanged, and existing sessions keep finding it.
- **CLAP** format, beside VST3, AU and the standalone application (CLAP id
  `com.fx-mechanics.ModalDish`), in every release archive and in the macOS
  installer.
- **FxmeNet support.** Under [FxmeNet](https://github.com/odoare/FxmeNet), the
  plate sounds on the server while the editor stays in the DAW:
  - the shape, the boundaries and the computed modes reach the server with the
    state, so it does not solve them again;
  - clicks on the plate, MIDI learn and the pickup panel's meter work through
    the editor;
  - the output and pickup meters and the hit markers show what the server
    plays.
  The animated displacement and velocity view is not shown under FxmeNet.
  Needs FxmeNet newer than 0.3.0.
- The window shows the exact build: `0.2.0` on a release, `0.2.0-3-gabc1234`
  for a build made three commits after it.

### Changed

- Built with JUCE 9 (was JUCE 8).
- Building from source: FxmeTools is no longer a git submodule. A plain
  `git clone` then `cmake -B build` fetches JUCE, FxmeTools and
  clap-juce-extensions at the commits pinned in `cmake/Dependencies.cmake`;
  sibling clones next to the repository (`../JUCE`, `../FxmeTools`) are used
  instead when present (`-DFXME_USE_SIBLINGS=OFF` turns that off).
- The version comes from the git tag only: there is no number to keep in step
  in `CMakeLists.txt` or the release workflow any more.
- Tooltip wording.

## [0.1.0] - 2026-09-04

First release.

[Unreleased]: https://github.com/odoare/ModalDish/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/odoare/ModalDish/releases/tag/v0.1.0
