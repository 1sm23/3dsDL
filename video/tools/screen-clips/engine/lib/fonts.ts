import {continueRender, delayRender, staticFile} from 'remotion';

// The website's fonts and Inter Tight (all SIL OFL 1.1), Latin subsets.
// M PLUS Rounded 1c stands in for the console's system font, as on the site.
const FACES: [family: string, weight: string, file: string][] = [
  ['M PLUS Rounded 1c', '400', 'm-plus-rounded-1c-400-latin.woff2'],
  ['M PLUS Rounded 1c', '500', 'm-plus-rounded-1c-500-latin.woff2'],
  ['M PLUS Rounded 1c', '700', 'm-plus-rounded-1c-700-latin.woff2'],
  ['M PLUS Rounded 1c', '800', 'm-plus-rounded-1c-800-latin.woff2'],
  ['M PLUS 1 Code', '500', 'm-plus-1-code-500-latin.woff2'],
  ['Gaegu', '700', 'gaegu-700-latin.woff2'],
  ['DotGothic16', '400', 'dotgothic16-400-latin.woff2'],
  ['Inter Tight', '400', 'inter-tight-latin-400-normal.woff2'],
  ['Inter Tight', '500', 'inter-tight-latin-500-normal.woff2'],
  ['Inter Tight', '600', 'inter-tight-latin-600-normal.woff2'],
  ['Inter Tight', '700', 'inter-tight-latin-700-normal.woff2'],
];

export const ROUNDED = '"M PLUS Rounded 1c", sans-serif';
export const CODE = '"M PLUS 1 Code", monospace';
export const HAND = 'Gaegu, cursive';
export const PIXEL = 'DotGothic16, monospace';
// The second cut's copy face (SIL OFL 1.1, via @fontsource/inter-tight).
export const SANS = '"Inter Tight", system-ui, sans-serif';

let loading: Promise<void> | null = null;

export const loadFonts = (): Promise<void> => {
  if (loading) return loading;
  const handle = delayRender('fonts');
  loading = Promise.all(
    FACES.map(async ([family, weight, file]) => {
      const face = new FontFace(family, `url(${staticFile(`fonts/${file}`)}) format('woff2')`, {weight});
      await face.load();
      document.fonts.add(face);
    }),
  ).then(() => continueRender(handle));
  return loading;
};
