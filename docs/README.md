# IronPhoenix website

GitHub Pages serves this folder directly. There is no compilation step, package manager, or generated output to publish. All runtime assets are local. `index.html` contains the content, `assets/site.css` contains the theme, and `assets/site.js` provides progressive enhancements (mobile navigation, command copying, and a decorative board). The engine itself does not run in the browser.

## Preview

From the repository root, with Python 3 installed:

```sh
python -m http.server 8080 --directory docs
```

Open http://localhost:8080. Check desktop and mobile layouts, the navigation button and Escape key, command copying, and keyboard focus. Content, links, and navigation remain available with JavaScript disabled; the decorative board requires JavaScript. Clipboard buttons appear only in secure contexts where the Clipboard API exists.

## Maintenance

- Keep engine claims and commands aligned with the root README and `nnue/README.md`.
- Bulma **1.0.4** is vendored from `https://cdn.jsdelivr.net/npm/bulma@1.0.4/css/bulma.min.css` in `assets/vendor/`. Its MIT license is included. To upgrade, replace the pinned CSS and license, update the link in `index.html`, and review both layouts.
- The system font stack and SVG mark require no external requests.
- Preserve `CNAME` exactly: it configures the existing custom domain, `iphoenix.art`.
- Keep the existing Pages deployment source and workflows. Changes here require no new workflow or hosting service.
- Relative asset links support both the custom domain and repository subpaths. If the domain changes, update the canonical and Open Graph URLs as well as the Pages domain setting.

Before a PR, run `git diff --check`, serve `docs/`, and verify local assets, section anchors, the 160-square board, keyboard navigation, and narrow-screen overflow. Only website files should change.
