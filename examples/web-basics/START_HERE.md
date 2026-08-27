# Start here — build and publish your first web page

*A numbered, first-session walkthrough. If you have never made a web page, do
exactly these steps once. You will end with a real, accessible, responsive page —
live on the internet at a link you can share.*

## The good news, up front

You need **no build tools** to make a web page — no npm, no webpack, no framework.
Just a text editor (jichi handles that) and a **web browser**. You write files,
open the `.html` in your browser, and refresh to see changes. That is the whole
setup. Do not let anyone scare you into a toolchain you do not need yet.

## Before you begin

You need **jichi** (you have it), a **web browser** (you have one), and **a model**
(copy `config.example.json`'s `models` block into your config; a local one is free
and private — `docs/LOCAL_MODELS.md`; then `jichi doctor`). Pick something small and
real to make a page about — you, a hobby, a project.

## Your first web page, step by step

1. **Open jichi here** (`cd` into this folder, run `jichi`).

2. **Build the page — content first.** Type:

   ```
   /page
   ```

   jichi (the `web-guide`) helps you write `index.html` with **semantic** tags — a
   real `<header>`, `<main>`, headings in order, paragraphs, a list, a link, an image
   with `alt` text. **Open `index.html` in your browser** (double-click it) and see
   your words appear. Refresh after each change. Notice: it already makes sense with
   no styling — that is the point.

3. **Make it look good.** Type:

   ```
   /style
   ```

   jichi adds a `styles.css` — fonts, colours, spacing, and layout with flexbox/grid.
   Refresh the browser after each tweak and watch it transform. The content did not
   change; only how it looks.

4. **Make it work on a phone.** Type:

   ```
   /responsive
   ```

   jichi adds the viewport meta tag, relative units, and a media query or two so the
   page adapts to any screen. **Drag your browser window narrow** (or use its device
   mode) and watch the layout reflow. A page that only works on desktop is half a
   page — most people are on phones.

5. **Check accessibility.** Before you publish it:

   ```
   Spawn the accessibility-reviewer agent to review my page.
   ```

   It reads (never edits) and flags a missing `alt`, a `<div>` that should be a
   `<button>`, headings out of order, low contrast — the things that quietly lock
   people out. Fixing them now is easy and makes your page work for everyone.

6. **Put it online.** Type:

   ```
   /deploy-static
   ```

   jichi walks you through a free host (GitHub Pages, or a drag-and-drop host) so
   your page gets a real URL. **Seeing your own page live at a link is a real
   milestone** — share it (with things you are happy to be public). Check every link
   and image works on the live site, and that it is responsive on a phone.

That is the whole arc: **page → style → responsive → accessible → live.** You built a
real web page, from nothing, with a text editor and a browser — and put it on the
internet. From here, keep improving it and re-deploying; every site is this, grown.

## If you get stuck

- The page is blank / nothing shows → open the actual `.html` file in the browser
  (File > Open), and check the tags are closed.
- It looks fine on desktop but broken on a phone → you likely miss the viewport meta
  tag, or use fixed pixel widths (`skills/responsive-design`).
- It works locally but is broken once deployed → almost always an absolute path;
  use relative paths (`styles.css`, not `/Users/me/.../styles.css`).
- You are reaching for `float` or `position: absolute` to lay things out → use
  flexbox/grid instead (`skills/css-layout`) — it is far easier.
