# Changelog

All notable changes are documented here. Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- Exact text from the application itself (UI Automation): browsers and other apps are searchable at any zoom, including pages whose text is too small for OCR. Highlights follow the real word positions in the pixels.
- `--no-page-text` option.
- OCR runs on several horizontal strips at once (about 1.6-1.9x faster), and shows its first result before any enlarged second pass.
- Automatic refresh while scrolling: mouse-wheel scrolling over the searched monitor hides stale highlights and re-reads the screen once scrolling pauses.

### Fixed
- Table cells (such as GitHub's "1 hour ago"), list items, menu items, tabs, checkboxes and column headers are now read through accessibility too; browsers expose their text only as the element name. Containers that merely repeat the items inside them are dropped, and a browser whose query fails is retried instead of being treated as empty.
- Small text (for example zoomed-out web pages) is now read: from the application's own text where it publishes it, otherwise by recognizing an enlarged copy of the same screenshot (no second capture).

### Changed
- F5 and automatic refreshes keep a dragged search bar in place when the cursor stays on the same monitor.

## [1.0.0]

- Initial release: global hotkey, Windows OCR, live search, highlight overlay, tray icon, desktop shortcut labels.
