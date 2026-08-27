---
name: semantic-html
description: Using HTML tags for their meaning, not just their look — the foundation of accessibility, SEO, and maintainable pages.
---
# Semantic HTML

Semantic HTML means using the tag that describes *what a thing is*, not just how it
looks. A `<button>` is a button; a heading is `<h2>`; navigation is `<nav>`. The
alternative — `<div>` for everything, styled to look right — is "div soup," and it
quietly breaks three things at once: accessibility, SEO, and your ability to
understand the page later.

**Why it matters (three payoffs from one habit):**

- **Accessibility.** Screen readers navigate by semantic structure — landmarks
  (`<header>`, `<nav>`, `<main>`, `<footer>`), headings, lists, buttons, links. A
  page of `<div>`s is, to a screen-reader user, a featureless wall. Semantic tags
  make it navigable for free.
- **Behaviour for free.** A `<button>` is keyboard-focusable and clickable with
  Enter/Space; an `<a href>` is a real link. A clickable `<div>` is none of those
  unless you reinvent them (and beginners never fully do).
- **Meaning for machines and future-you.** Search engines and your own eyes both
  read the structure. `<article>` says "this is a self-contained piece"; `<div>`
  says nothing.

**The tags to reach for:**

- **Landmarks:** `<header>`, `<nav>`, `<main>` (one per page), `<article>`,
  `<section>`, `<aside>`, `<footer>`.
- **Headings:** `<h1>` once, then `<h2>`-`<h6>` *in order* (do not skip levels; do not
  pick a heading for its size — use CSS for size).
- **Text & lists:** `<p>`, `<ul>`/`<ol>`/`<li>`, `<blockquote>`, `<figure>`/
  `<figcaption>`.
- **Interactive:** `<a href>` to go somewhere, `<button>` to do something, `<form>`
  with `<label>`ed inputs.
- **Media:** `<img>` with a meaningful `alt` (or `alt=""` if decorative).

**The test:** read your HTML with all the CSS removed. Does it still make sense as a
structured document — a clear outline, obvious links and buttons? If yes, it is
semantic. If it is a meaningless stack of boxes, reach for the real tags. Getting
this right from the start costs nothing; retrofitting it later is a slog.
