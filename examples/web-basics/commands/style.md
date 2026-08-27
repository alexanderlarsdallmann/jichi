---
description: Add CSS to style and lay out a page — colours, type, spacing, and layout with flexbox/grid — keeping the HTML readable without it.
---
Help the user style their page with CSS (in a `styles.css` linked from the
`<head>`, not inline). The HTML/content is already right (`/page`); now make it look
good — and keep it so the page still makes sense if the CSS never loads.

Work up in layers:

1. **The basics** — a readable font stack, comfortable font size and line-height,
   sensible colours and spacing, a max-width on the content so lines are not too
   long. These alone make a page look intentional.
2. **Layout** — position things with **flexbox** (for rows/columns of items) or
   **grid** (for two-dimensional layouts). Explain which fits the case; avoid old
   hacks (floats for layout, absolute positioning everywhere). See the `css-layout`
   skill.
3. **Colour and contrast** — pick colours that meet **contrast** guidelines so text
   is readable; never rely on colour alone to convey meaning (a11y).
4. **Consistency** — reuse spacing/colour values (CSS custom properties, i.e. CSS
   variables, help)
   so the design is coherent.

Add a little, **refresh the browser, see it**, adjust — CSS is a tight
edit-refresh loop; use it. Keep selectors simple and the file readable. Do NOT
sacrifice the semantic HTML for a visual trick — the structure stays sound.
Suggest `/responsive` to make it work on phones next, and the
`accessibility-reviewer` to check contrast and that styling did not break structure.
