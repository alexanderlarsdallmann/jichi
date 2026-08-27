---
description: Make the page work on any screen — mobile-first, relative units, a media query or two. A page that only works on desktop is half a page.
---
Help the user make their page **responsive** — work well on a phone, a tablet, and a
desktop. Most of the web is browsed on phones; a desktop-only page is half-built.
The good news: responsive design is mostly a few habits, not a framework.

1. **Confirm the viewport meta** is present (`<meta name="viewport"
   content="width=device-width, initial-scale=1">`) — without it, phones pretend to
   be desktops and shrink everything. This one line is the most common miss.
2. **Use relative units.** Prefer `rem`/`em`, `%`, and viewport units over fixed
   `px` for sizes and spacing, and `max-width` (not fixed `width`) so content
   shrinks to fit. Images: `max-width: 100%; height: auto;` so they never overflow.
3. **Design mobile-first.** Style the small screen first (it forces you to keep the
   essentials), then use `@media (min-width: ...)` queries to *add* layout for wider
   screens — a couple of breakpoints is usually enough.
4. **Let flexbox/grid reflow.** A flex row that `flex-wrap`s, or a grid that changes
   column count at a breakpoint, adapts with little code.

Test by **resizing the browser window** (drag it narrow) and using the browser's
device/responsive mode — watch the layout adapt, and fix what breaks (text too small,
content overflowing, a horizontal scrollbar). See the `responsive-design` skill.
Then it is ready to share — suggest `/deploy-static`.
