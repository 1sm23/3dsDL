// The app's interface icons, generated from the SVG bodies in
// scripts/make-ui-icons.py (the same source the console's atlas is rendered
// from), so they stay sharp at any size. 24 x 24 grid, round 2 px strokes.

export type IconPart = {d: string} | {rect: [number, number, number, number, number]} | {circle: [number, number, number]};

export const ICON_PARTS = {
  AppMark: [{d: "M7 3.5h7.5L19 8v11a1.5 1.5 0 0 1-1.5 1.5h-10A1.5 1.5 0 0 1 6 19V5a1.5 1.5 0 0 1 1-1.5z"}, {d: "M12.5 8.5v7M9.5 12.5l3 3 3-3"}, {d: "M9.5 18h6"}],
  Scan: [{d: "M4 9V5a1 1 0 0 1 1-1h4M15 4h4a1 1 0 0 1 1 1v4M20 15v4a1 1 0 0 1-1 1h-4M9 20H5a1 1 0 0 1-1-1v-4"}, {rect: [8, 8, 3, 3, 0]}, {rect: [13, 8, 3, 3, 0]}, {rect: [8, 13, 3, 3, 0]}, {d: "M13 13h3v3"}],
  Globe: [{circle: [12, 12, 8]}, {d: "M4 12h16M12 4c2.4 2.3 3.5 5 3.5 8s-1.1 5.7-3.5 8c-2.4-2.3-3.5-5-3.5-8s1.1-5.7 3.5-8z"}],
  History: [{d: "M4.5 12a7.5 7.5 0 1 0 2.2-5.3"}, {d: "M4 4.5v3.5h3.5"}, {d: "M12 8v4.5l3 2"}],
  Folder: [{d: "M3.5 7.5a1.5 1.5 0 0 1 1.5-1.5h4l2 2h8a1.5 1.5 0 0 1 1.5 1.5v8a1.5 1.5 0 0 1-1.5 1.5H5a1.5 1.5 0 0 1-1.5-1.5z"}],
  Wrench: [{d: "M14.5 6.5a4 4 0 0 0 4.8 5.2L12 19l-2.5.5L9 17l7.3-7.3"}, {d: "M14.5 6.5L17 4a4 4 0 0 1 3 4.7"}],
  Download: [{d: "M12 4v10M8 10l4 4 4-4"}, {d: "M4 15v3a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-3"}],
  Package: [{d: "M4 8l8-4 8 4-8 4z"}, {d: "M4 8v8l8 4 8-4V8"}, {d: "M12 12v8"}],
  Picture: [{rect: [4, 5, 16, 14, 1.5]}, {circle: [9, 10, 1.6]}, {d: "M5 17l5-5 4 4 2-2 4 4"}],
  Text: [{d: "M7 3.5h7l4.5 4.5v11a1.5 1.5 0 0 1-1.5 1.5H7A1.5 1.5 0 0 1 5.5 19V5A1.5 1.5 0 0 1 7 3.5z"}, {d: "M9 12h6M9 15.5h6M9 8.5h3"}],
  Zip: [{rect: [5, 3.5, 14, 17, 1.5]}, {d: "M12 3.5v2M12 7v2M12 10.5v1.5"}, {rect: [10, 13, 4, 4, 1]}],
  App: [{rect: [4, 4, 16, 16, 3]}, {d: "M9 12l2 2 4-4"}],
  GameCard: [{d: "M6 3.5h9l3 3V20a.5.5 0 0 1-.5.5h-11A.5.5 0 0 1 6 20z"}, {d: "M9 8h6v5H9z"}, {d: "M9 16.5h6"}],
  Hex: [{d: "M8 4H6v16h2M16 4h2v16h-2"}, {d: "M10.5 9.5v5M13.5 9.5v5"}],
  File: [{d: "M7 3.5h7l4.5 4.5v11a1.5 1.5 0 0 1-1.5 1.5H7A1.5 1.5 0 0 1 5.5 19V5A1.5 1.5 0 0 1 7 3.5z"}, {d: "M14 3.5V8h4.5"}],
  Trash: [{d: "M5 7h14M10 4h4M7 7l1 12.5a1 1 0 0 0 1 .9h6a1 1 0 0 0 1-.9L17 7"}],
  Clock: [{circle: [12, 12.5, 8]}, {d: "M12 8.5v4.5l3 1.8"}],
  Up: [{d: "M9 14l-4-4 4-4"}, {d: "M5 10h9a5 5 0 0 1 5 5v3"}],
  Music: [{d: "M9 18V6l10-2v12"}, {circle: [6.5, 18, 2.5]}, {circle: [16.5, 16, 2.5]}],
  Video: [{rect: [3.5, 6, 12, 12, 1.5]}, {d: "M15.5 10.5l5-3v9l-5-3"}],
  Warning: [{d: "M12 4l9 16H3z"}, {d: "M12 10v4M12 17v.5"}],
  Check: [{d: "M5 12.5l4.5 4.5L19 7.5"}],
  Chip: [{rect: [6, 6, 12, 12, 1.5]}, {d: "M9 3v3M15 3v3M9 18v3M15 18v3M3 9h3M3 15h3M18 9h3M18 15h3"}],
  Lock: [{rect: [5, 11, 14, 9, 1.5]}, {d: "M8 11V8a4 4 0 0 1 8 0v3"}],
} satisfies Record<string, IconPart[]>;

export type IconName = keyof typeof ICON_PARTS;

// The atlas sizes and the stroke each is drawn with (in grid units).
export const ATLAS_SIZES = [64, 32, 24, 18];
export const ATLAS_STROKE: Record<number, number> = {64: 1.7, 32: 2.0, 24: 2.0, 18: 2.1};
