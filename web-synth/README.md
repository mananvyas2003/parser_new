# Circuit Synthesis Compiler (Vercel)

Static browser demo — runs entirely in the browser (no C++ binary, no API server).

## Project layout

```
web-synth/
  public/
    index.html   ← UI
    synth.js     ← full synthesis pipeline (client-side)
  vercel.json
  package.json
```

## Vercel dashboard settings (important)

Because the site lives under `public/`, set:

| Setting | Value |
|---|---|
| **Root Directory** | `web-synth/public` |
| **Framework Preset** | Other |
| **Build Command** | leave empty |
| **Output Directory** | leave empty |
| **Install Command** | leave empty |

If you set Root Directory to `web-synth` instead of `web-synth/public`, the homepage will 404.

## Local check

```bash
cd web-synth
npm run dev
```

Open http://localhost:3000

## CLI deploy

```bash
cd web-synth/public
npx vercel
```
