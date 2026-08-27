# Web-basics project conventions

*This file tells the agent how to behave in a beginner web workspace. jichi loads
it automatically. Keep it short; edit it to match your project.*

## What this bench is (and the good news)

A bench for building your **first web page or small static site** — HTML, CSS, and
putting it online. And the liberating truth: **you do not need any build tools for
this.** No npm, no webpack, no framework — a text editor and a browser. You write
`.html` and `.css` files, open the `.html` in a browser (double-click it, or serve
the folder locally), and refresh to see your changes. jichi writes and edits the
files; you view the result in the browser.

## The order, and the two habits

**Build HTML → CSS → (later) JS.** This is how the web is meant to work: content
and structure first (HTML), presentation on top (CSS), behaviour last and only if
needed (JS). The page should make sense as plain HTML before any styling.

Two habits, from day one — cheap now, painful to retrofit:

1. **Semantic HTML.** Use the tag that means the thing — `<header>`, `<nav>`,
   `<main>`, `<article>`, `<footer>`, ordered `<h1>`-`<h6>`, `<button>` for buttons,
   `<a>` for links, `<ul>` for lists — not a pile of `<div>`s (`skills/semantic-html`).
2. **Accessibility.** Every image has meaningful `alt` (or `alt=""` if decorative);
   headings in order; links say where they go; colour is never the only signal; it
   works with the keyboard. Build it in; do not bolt it on.

## The rules of this bench

- **Content still makes sense with no CSS.** Structure is sound on its own.
- **It works on a phone.** Viewport meta tag, relative units, `max-width: 100%` on
  images, mobile-first (`skills/responsive-design`). A desktop-only page is half a
  page.
- **Layout with flexbox/grid**, not float hacks (`skills/css-layout`).
- **Relative paths** (`styles.css`, `images/pic.png`), so it works when deployed, not
  just on your machine.
- **Small steps, viewed in the browser.** Add a little, refresh, see it — the browser
  is your instant feedback.

## The workflow (commands)

- `/page` — create a semantic, accessible HTML page.
- `/style` — style and lay it out with CSS (readable without the CSS).
- `/responsive` — make it work on any screen (mobile-first).
- `/deploy-static` — put it online for free (GitHub Pages / a drag-and-drop host).

Use the read-only **`accessibility-reviewer`** agent as you go — it flags a missing
`alt`, div-soup, broken headings, low contrast, a clickable div that should be a
button. Accessibility is a habit, and this keeps it one.
