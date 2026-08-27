---
description: Create or structure an HTML page with SEMANTIC markup — the right tags for the meaning, accessible from the start.
---
Help the user create or improve an HTML page (`index.html`), with **semantic,
accessible** markup from the start. Content and structure first; styling comes
later (`/style`). No build tools — this is a file you open in a browser.

Build the page with the tags that mean the thing:

1. **The document shell** — `<!doctype html>`, `<html lang="en">`, a `<head>` with a
   `<meta charset>`, a `<meta name="viewport" ...>` (so it works on phones), and a
   real `<title>`.
2. **Semantic structure** — `<header>` (with the site title / `<nav>`), `<main>`
   (the primary content, one per page), `<article>`/`<section>` as appropriate,
   `<footer>`. Not a stack of `<div>`s.
3. **Content with meaning** — one `<h1>`, then `<h2>`-`<h6>` in order (no skipping);
   `<p>` for text; `<ul>`/`<ol>` for lists; `<a href>` for links (with text that says
   where it goes); `<img src alt>` with meaningful `alt` on every image.
4. **Accessibility built in** — every image has `alt`, headings are ordered, links
   are descriptive, and interactive things are `<button>`/`<a>` (not clickable divs).

Explain what each tag is *for*, so the learner builds a mental model. After each
addition, have them **open the page in a browser and refresh** to see it — the HTML
should already make sense with no CSS at all. Then suggest `/style`, and the
read-only `accessibility-reviewer` to catch a missing `alt` or a div that should be
semantic. See the `semantic-html` skill.
