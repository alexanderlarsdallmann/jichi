---
name: css-layout
description: Laying out a page with flexbox and grid — the modern tools that replace the old float/positioning hacks beginners get stuck on.
---
# CSS layout: flexbox and grid

Positioning things on a page is where most beginners get stuck — often because they
learned old hacks (floats for layout, `position: absolute` everywhere) that fight
back. Modern CSS gives you two purpose-built tools that make layout
straightforward: **flexbox** and **grid**. Learn these two and most layout problems
become easy.

**Flexbox — for one dimension (a row OR a column).** Perfect for a navbar, a row of
cards, centering something, spacing items evenly.

```css
.row {
  display: flex;
  gap: 1rem;                /* space between items -- no margin hacks */
  align-items: center;      /* vertical alignment in a row */
  justify-content: space-between;  /* horizontal distribution */
}
```

Add `flex-wrap: wrap` and the items wrap to new lines when they run out of room —
half of responsive design, for free.

**Grid — for two dimensions (rows AND columns).** Perfect for a page layout, a photo
gallery, any real grid.

```css
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 1rem;
}
```

That one line makes a gallery that fits as many 200px+ columns as the screen allows
and reflows on smaller screens — responsive with no media query.

**Which to use:** a single row or column of things → **flexbox**. A true
two-dimensional layout → **grid**. They compose — a grid whose cells contain flex
rows is common and fine.

**Habits that avoid pain:**

- Use `gap` for spacing between items, not margins on each child.
- Use `max-width` + `margin: 0 auto` to center a content column and cap line length.
- Reach for flex/grid *before* `float` or absolute positioning — those are for
  special cases, not general layout.
- Keep it simple: most pages need a couple of flex rows and maybe one grid.

Change a property, **refresh the browser**, see what it did — CSS layout is learned
by fiddling with a live page far faster than by reading.
