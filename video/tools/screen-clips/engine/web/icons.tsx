import React from 'react';

// The page's icon set, copied from ICONS in source/core/remote_page.cpp.
export const ICONS = {
  upload: '<path d="M12 16V4M7 9l5-5 5 5M4 20h16"/>',
  folder: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
  folderplus: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2zM12 10v6M9 13h6"/>',
  refresh: '<path d="M20 11a8 8 0 1 0-2.3 5.7M20 4v7h-7"/>',
  search: '<circle cx="11" cy="11" r="7"/><path d="m20 20-4-4"/>',
  download: '<path d="M12 4v12M7 11l5 5 5-5M4 20h16"/>',
  eye: '<path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
  edit: '<path d="M4 20h4L19 9l-4-4L4 16zM14 6l4 4"/>',
  install: '<path d="M12 3v10M8 9l4 4 4-4M4 15v3a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-3"/>',
  copy: '<rect x="9" y="9" width="11" height="11" rx="2"/><path d="M5 15V6a2 2 0 0 1 2-2h8"/>',
  rename: '<path d="M4 7V5h16v2M12 5v14M9 19h6"/>',
  move: '<path d="M5 12h14M13 6l6 6-6 6"/>',
  trash: '<path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13"/>',
  x: '<path d="M6 6l12 12M18 6 6 18"/>',
  check: '<path d="M5 12l5 5L20 7"/>',
  up: '<path d="M12 19V5M5 12l7-7 7 7"/>',
  warn: '<path d="M12 3 2 21h20zM12 10v4M12 17.5v.5"/>',
  info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v.5"/>',
  link: '<path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1"/>',
  lock: '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>',
  console: '<rect x="6" y="2.5" width="12" height="19" rx="2"/><path d="M8.5 5h7v5h-7zM10 15h4M12 13v4"/>',
  chevron: '<path d="M6 9l6 6 6-6"/>',
  out: '<path d="M7 17 17 7M9 7h8v8"/>',
  more: '<circle cx="5" cy="12" r="1.7"/><circle cx="12" cy="12" r="1.7"/><circle cx="19" cy="12" r="1.7"/>',
} as const;

export type IconName = keyof typeof ICONS;

export const Svg: React.FC<{name: IconName; style?: React.CSSProperties}> = ({name, style}) => (
  <svg
    className={'i' + (name === 'more' ? ' fill' : '')}
    viewBox="0 0 24 24"
    aria-hidden="true"
    style={style}
    dangerouslySetInnerHTML={{__html: ICONS[name]}}
  />
);

// FOLDER in remote_page.cpp: the yellow folder badge for directories.
export const FolderArt: React.FC = () => (
  <svg viewBox="0 0 26 26" aria-hidden="true">
    <path d="M2 6.5A2.5 2.5 0 0 1 4.5 4h5.6l2.4 2.6h9A2.5 2.5 0 0 1 24 9.1V20a2.5 2.5 0 0 1-2.5 2.5h-17A2.5 2.5 0 0 1 2 20z" fill="#e6b422" />
    <path d="M2 10h22v10a2.5 2.5 0 0 1-2.5 2.5h-17A2.5 2.5 0 0 1 2 20z" fill="#f7d55c" />
  </svg>
);

// KINDS in remote_page.cpp: which colour a file's badge gets.
const KINDS: Record<string, string> = {
  cia: 'cia', '3dsx': '3dsx', zip: 'zip', '7z': 'zip', rar: 'zip', png: 'img', jpg: 'img', jpeg: 'img', gif: 'img', bmp: 'img', webp: 'img',
  txt: 'txt', ini: 'txt', cfg: 'txt', json: 'txt', xml: 'txt', log: 'txt', md: 'txt', yml: 'txt', yaml: 'txt', lua: 'txt', nds: 'rom', '3ds': 'rom', cci: 'rom',
  firm: 'rom', tik: 'rom', smdh: 'rom', mp3: 'media', ogg: 'media', wav: 'media', mp4: 'media', bcstm: 'media',
};

export const kindOf = (x: string) => KINDS[x] || 'file';

// badge() in remote_page.cpp.
export const Badge: React.FC<{name: string; dir?: boolean}> = ({name, dir}) => {
  if (dir)
    return (
      <div className="ic k-dir">
        <FolderArt />
      </div>
    );
  const i = name.lastIndexOf('.');
  const x = i > 0 ? name.slice(i + 1).toLowerCase() : '';
  return <div className={'ic k-' + kindOf(x)}>{(x || 'file').slice(0, 4).toUpperCase()}</div>;
};
