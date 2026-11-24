# Changelog

All notable changes are documented here. Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- Automatic refresh while scrolling: mouse-wheel scrolling over the searched monitor hides stale highlights and re-reads the screen once scrolling pauses.

### Fixed
- Small text (for example zoomed-out web pages) is now read: when the first pass finds little or very small text, the monitor is captured and recognized again at 2x.

### Changed
- F5 and automatic refreshes keep a dragged search bar in place when the cursor stays on the same monitor.

## [1.0.0]

- Initial release: global hotkey, Windows OCR, live search, highlight overlay, tray icon, desktop shortcut labels.
