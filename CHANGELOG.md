# Changelog

## 1.2.0

### Large-file performance

- Reworked minimap rendering to aggregate matches into cached screen-pixel
  buckets. The minimap now emits a small, fixed amount of geometry instead of
  one rectangle per matched line on every frame.
- Changed regex scanning from a single vectored operation to bounded streaming
  chunks. Stale searches now observe cancellation even when the pattern has no
  matches, while matches spanning chunk boundaries retain whole-file semantics.
- Added a configurable retained-result limit, defaulting to 2,000,000 matches,
  to prevent broad expressions from exhausting memory. The status bar reports
  when results have reached the limit.
- Moved match overlap and matched-line index construction to the search worker,
  reducing result acceptance on the UI thread to vector swaps.
- Replaced capture-group cache-wide clears with bounded LRU eviction, avoiding
  periodic deallocation spikes while scrolling.
- Changed the shared file mapping to the kernel's normal access policy so
  sequential background scans do not penalize random and backward scrolling.
