---
description: Put your static site online for free — GitHub Pages, Netlify, or similar — so you can share a real URL.
---
Help the user deploy their static site (HTML/CSS/JS files, no server needed) to the
web for free, so they have a real URL to share. Seeing your own page live at a link
is a genuine milestone — it makes the whole thing real.

First confirm the site is **static** (just files — no backend/database) and works
locally (open `index.html` in a browser). Then walk a free option; a couple of good
ones:

1. **GitHub Pages** — put the files in a GitHub repository and enable Pages in the
   repo settings (serving from the main branch). Great if they already use git; the
   site lives at a `github.io` URL. Note the folder Pages serves from and that it can
   take a minute to go live.
2. **Netlify / similar drag-and-drop hosts** — some let you drag the site folder onto
   a web dashboard and get a URL instantly, no git required. Easiest for a first
   deploy.
3. **Any static host** — the same files work anywhere that serves static files.

Whichever they choose:

- **Check paths are relative** (`styles.css`, `images/pic.png`), not absolute paths
  that only work on their machine — the #1 "works locally, broken when deployed" bug.
- **Verify it live** — open the deployed URL, click every link, check images load and
  the page is responsive on a phone. A deploy that looks broken online usually means
  a wrong path.
- **Write the deploy steps in the README** so they (and others) can update it again.

Celebrate the live URL — then keep improving the page and re-deploying. Frame
sharing carefully: only put up content they are comfortable being public.
