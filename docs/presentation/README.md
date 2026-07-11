# OPINE presentation

A [reveal.js](https://revealjs.com) slide deck introducing OPINE to a
general developer audience. Show-don't-tell: nearly every slide is a
code snippet plus the real output of running it (all outputs are
unedited runs of the programs in [`examples/`](../../examples/)).

## Presenting

Open `index.html` in a browser. reveal.js and its plugins load from a
CDN, so you need a network connection (or swap the four CDN URLs for
a local copy of reveal.js).

- **→ / ←** — next / previous slide (some slides build in fragments)
- **S** — speaker view: notes, timer, and next-slide preview
- **F** — fullscreen
- **Esc** — slide overview

## Editing

It's a single self-contained HTML file. Slides are `<section>`
elements; speaker notes are the `<aside class="notes">` inside each
one. Code samples are HTML-escaped (`&lt;` for `<`), terminal output
goes in `<pre class="console">` blocks.

If you change a number on a slide, re-run the example it came from —
every output shown is meant to be reproducible.
