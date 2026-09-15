#!/usr/bin/env node
/**
 * Render submission SVG figures to PNG with Chinese font support (msyh).
 */
import { Resvg } from '@resvg/resvg-js';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const ASSETS = path.join(ROOT, 'assets/readme');
const OUT = path.join(__dirname, 'figures');
const FONT_DIRS = [
  path.join(__dirname, 'fonts'),
  '/mnt/c/Windows/Fonts',
];

const FILES = ['system-map.svg', 'agent-boundary.svg', 'data-flow.svg'];

function injectFontStyle(svgText) {
  const styleBlock = `<style>
    text, tspan { font-family: 'Microsoft YaHei', 'PingFang SC', sans-serif; }
  </style>`;
  if (svgText.includes('<style>')) {
    return svgText.replace(
      /text \{ font-family:[^}]+\}/g,
      "text, tspan { font-family: 'Microsoft YaHei', 'PingFang SC', sans-serif; }",
    );
  }
  return svgText.replace(/(<svg[^>]*>)/, `$1\n  ${styleBlock}`);
}

fs.mkdirSync(OUT, { recursive: true });

for (const name of FILES) {
  const src = path.join(ASSETS, name);
  if (!fs.existsSync(src)) {
    console.error('missing', src);
    process.exit(1);
  }
  let svg = fs.readFileSync(src, 'utf8');
  svg = injectFontStyle(svg);
  const resvg = new Resvg(Buffer.from(svg, 'utf8'), {
    fitTo: { mode: 'width', value: 1600 },
    background: '#ffffff',
    font: {
      loadSystemFonts: true,
      fontDirs: FONT_DIRS,
      defaultFontFamily: 'Microsoft YaHei',
      sansSerifFamily: 'Microsoft YaHei',
    },
  });
  const png = resvg.render().asPng();
  const dest = path.join(OUT, name.replace('.svg', '.png'));
  fs.writeFileSync(dest, png);
  console.log('wrote', dest, png.length);
}
