# Positional CSS variable optimization

Kestrel uses --right-width both for grid-template-columns and for the toast stack's
right: calc(var(--right-width) + 16px). The dimension-only fast path introduced in
PR #51 therefore fell back to a full subtree recalculation on right-divider drags.

Accept physical left/right/top/bottom offsets in the same conservative geometry
property set. Apply the existing inheritance, alias, shadow-DOM and selector
fallbacks to these properties too. No Kestrel or graphics backend changes are
included.

The new native test combines a grid track with all four calc-based offsets and
checks setProperty, removeProperty and the empty setter. The benchmark now also
checks the fixed toast offset on every update, covering the dependency missed by
the earlier dimension-only benchmark.

Validation was performed with this implementation integrated into the Windows
Kestrel worktree, before extracting this main-based draft:
- Native build and all 15 focused compatibility checks passed, including the
  previous explicit-inheritance regressions.
- Three alternating expanded benchmark runs measured median-of-medians 22.69 ms
  before versus 3.49 ms after (approximately 85% less synchronous style/layout time).
- The attempted live drag verification was invalid because the display changed
  to an 822px CSS viewport, hiding the properties sidebar; vsync was unavailable.
  No post-fix application FPS or physical-presentation claim is made.

This extracted main-based branch has not yet had a separate native build.
