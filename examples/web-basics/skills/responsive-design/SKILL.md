---
name: responsive-design
description: Making a page work on any screen size — the viewport meta, relative units, mobile-first, and a media query or two.
---
# Responsive design

Most people will see your page on a phone. A page that only works on a desktop is
half-built — and fixing it is a handful of habits, not a framework. Responsive
design means the page adapts to whatever screen it is on.

**The four things that get you most of the way:**

1. **The viewport meta tag** — this ONE line, in your `<head>`, is the most common
   miss:

   ```html
   <meta name="viewport" content="width=device-width, initial-scale=1">
   ```

   Without it, phones render the page at desktop width and shrink it to fit —
   tiny, unusable text. With it, the phone uses its real width.

2. **Relative units, not fixed pixels.** Use `rem`/`em` for type and spacing, `%`
   and viewport units where they fit, and — crucially — `max-width` instead of a
   fixed `width` so content shrinks to fit small screens. Images must not overflow:

   ```css
   img { max-width: 100%; height: auto; }
   ```

3. **Mobile-first.** Write your base styles for the *small* screen first — it forces
   you to keep only the essentials — then *add* to it for larger screens with
   min-width media queries:

   ```css
   /* base = mobile */
   .cards { display: flex; flex-direction: column; gap: 1rem; }

   /* wider screens: lay the cards in a row */
   @media (min-width: 40rem) {
     .cards { flex-direction: row; }
   }
   ```

   A couple of breakpoints (a phone/desktop split, maybe a tablet one) is usually
   enough. Do not chase every device size.

4. **Let flex/grid reflow.** `flex-wrap: wrap` and grid's `auto-fit` (see
   `css-layout`) adapt to width with almost no code — lean on them.

**Test it constantly:** drag your browser window narrow and watch the layout adapt,
and use the browser's device/responsive mode. Watch for the classic breaks — text
too small (missing viewport meta), content overflowing (fixed widths, an image with
no `max-width`), or a horizontal scrollbar (something wider than the screen). Fix as
you go; responsive is much easier built in than bolted on.
