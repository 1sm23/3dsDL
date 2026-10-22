import React from 'react';
import {Img, staticFile} from 'remotion';
import {EASE, IN_OUT, SMOOTH, SPRING, mix, pop, prog} from './anim';
import {Page} from './Page';
import {CHROME_H, TAB_H, TOOL_H, cursorAt, hoverKey, pressAmount, pressKey, CLICKS} from './cursor';
import {M, contentLeft, contentW, rowTop, scrollAt} from './geometry';
import {ADDRESS_TYPED, DOWNLOADED_FILE, T, UP, folderAt} from './script';
import {kindOf} from './icons';
import {ext} from './anim';
import {RECEIVE_ADDRESS} from '../scripts/receive';

// A neutral desktop browser window showing the console's page at
// http://3ds.local/, with the mouse that drives it. Everything is a function
// of t, seconds from the start of the Receive scene.

const UI_FONT = 'system-ui,-apple-system,"Segoe UI",sans-serif';
const HOST = RECEIVE_ADDRESS.replace(/^https?:\/\//, '').replace(/\/$/, '');

const C = {
  frame: '#e9e7e3',
  bar: '#fbfaf8',
  line: '#d9d6d0',
  field: '#efedea',
  ink: '#2b2a28',
  soft: '#77736c',
  faint: '#a29e97',
  accent: '#3d7ff0',
};

const caretBlink = (t: number, since: number) => ((t - since) % 1.06 + 1.06) % 1.06 < 0.53;

// The route part of the address, as the page's hash routes set it.
const routeAt = (t: number) => {
  if (t < T.filesClick) return '';
  const p = t >= T.shotsShown - 0.1 ? '/luma/screenshots' : t >= T.lumaShown - 0.1 ? '/luma' : '/';
  return '/#files:' + encodeURIComponent(p);
};

const Glyph: React.FC<{d: string; size?: number; color?: string; width?: number}> = ({d, size = 18, color = C.soft, width = 1.9}) => (
  <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke={color} strokeWidth={width} strokeLinecap="round" strokeLinejoin="round">
    <path d={d} />
  </svg>
);

const Spinner: React.FC<{t: number; size?: number}> = ({t, size = 14}) => (
  <svg width={size} height={size} viewBox="0 0 16 16" style={{transform: `rotate(${t * 400}deg)`}}>
    <circle cx="8" cy="8" r="6" fill="none" stroke="#d6d3cd" strokeWidth="2" />
    <path d="M8 2a6 6 0 0 1 6 6" fill="none" stroke={C.accent} strokeWidth="2" strokeLinecap="round" />
  </svg>
);

const Ring: React.FC<{p: number; size: number; stroke?: number; done?: boolean}> = ({p, size, stroke = 2.4, done}) => {
  const r = size / 2 - stroke;
  const c = 2 * Math.PI * r;
  return (
    <svg width={size} height={size} style={{position: 'absolute', inset: 0}}>
      <circle cx={size / 2} cy={size / 2} r={r} fill="none" stroke="#dcd9d3" strokeWidth={stroke} />
      <circle
        cx={size / 2}
        cy={size / 2}
        r={r}
        fill="none"
        stroke={done ? '#2f9e5b' : C.accent}
        strokeWidth={stroke}
        strokeLinecap="round"
        strokeDasharray={`${c * p} ${c}`}
        transform={`rotate(-90 ${size / 2} ${size / 2})`}
      />
    </svg>
  );
};

// --- Title bar and toolbar ------------------------------------------------------------
const TabStrip: React.FC<{t: number}> = ({t}) => {
  const loading = t >= T.enter && t < T.pageOpens;
  const loaded = t >= T.pageOpens;
  const title = loaded ? '3dsDL' : loading ? HOST : 'New Tab';
  return (
    <div style={{position: 'absolute', left: 0, right: 0, top: 0, height: TAB_H, background: C.frame}}>
      {['#ff5f57', '#febc2e', '#28c840'].map((c, i) => (
        <div
          key={c}
          style={{position: 'absolute', left: 16 + i * 20, top: TAB_H / 2 - 6, width: 12, height: 12, borderRadius: 6, background: c, boxShadow: 'inset 0 0 0 0.5px rgba(0,0,0,.18)'}}
        />
      ))}
      <div
        style={{
          position: 'absolute',
          left: 84,
          bottom: 0,
          width: 232,
          height: 34,
          background: C.bar,
          borderRadius: '10px 10px 0 0',
          boxShadow: '0 0 0 0.5px rgba(0,0,0,.08)',
          display: 'flex',
          alignItems: 'center',
          gap: 8,
          padding: '0 10px 0 12px',
          font: `500 13px ${UI_FONT}`,
          color: C.ink,
        }}
      >
        <div style={{width: 16, height: 16, flex: 'none', display: 'grid', placeItems: 'center'}}>
          {loading ? (
            <Spinner t={t} />
          ) : loaded ? (
            <Img src={staticFile('brand/icon-256.png')} style={{width: 16, height: 16, borderRadius: 4}} />
          ) : (
            <Glyph d="M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM3 12h18M12 3c2.5 2.6 3.8 5.6 3.8 9s-1.3 6.4-3.8 9c-2.5-2.6-3.8-5.6-3.8-9S9.5 5.6 12 3z" size={14} color={C.faint} width={1.6} />
          )}
        </div>
        <span style={{flex: 1, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis'}}>{title}</span>
        <Glyph d="M7 7l10 10M17 7 7 17" size={14} color={C.faint} width={1.8} />
      </div>
      <div style={{position: 'absolute', left: 326, top: TAB_H / 2 - 9}}>
        <Glyph d="M12 5v14M5 12h14" size={18} color={C.soft} width={1.7} />
      </div>
    </div>
  );
};

const Toolbar: React.FC<{t: number; width: number}> = ({t, width}) => {
  const focused = t >= T.addrClick && t < T.enter;
  const typed = t < T.typeStart ? '' : ADDRESS_TYPED.slice(0, Math.round(ADDRESS_TYPED.length * prog(t, T.typeStart, T.typeEnd)));
  const navigated = t >= T.enter;
  const route = routeAt(t);
  const dlShown = t >= T.downloadStart;
  const dlPop = SPRING(prog(t, T.downloadStart, T.downloadStart + 0.35));
  const dlP = SMOOTH(prog(t, T.downloadStart, T.downloadDone));
  const done = t >= T.downloadDone;
  return (
    <div
      style={{
        position: 'absolute',
        left: 0,
        right: 0,
        top: TAB_H,
        height: TOOL_H,
        background: C.bar,
        borderBottom: `1px solid ${C.line}`,
        display: 'flex',
        alignItems: 'center',
        gap: 6,
        padding: '0 12px',
        boxSizing: 'content-box',
      }}
    >
      <div style={{display: 'flex', gap: 14, padding: '0 8px 0 4px', alignItems: 'center'}}>
        <Glyph d="M19 12H5M11 6l-6 6 6 6" color={navigated ? C.soft : C.faint} />
        <Glyph d="M5 12h14M13 6l6 6-6 6" color={C.faint} />
        {t >= T.enter && t < T.pageOpens ? (
          <Glyph d="M6 6l12 12M18 6 6 18" color={C.soft} />
        ) : (
          <Glyph d="M20 11a8 8 0 1 0-2.3 5.7M20 4v7h-7" color={C.soft} />
        )}
      </div>
      <div
        style={{
          flex: 1,
          height: 32,
          borderRadius: 16,
          background: focused ? '#fff' : C.field,
          boxShadow: focused ? `0 0 0 2px ${C.accent}88` : 'none',
          display: 'flex',
          alignItems: 'center',
          gap: 8,
          padding: '0 14px',
          font: `14px ${UI_FONT}`,
          color: C.ink,
          whiteSpace: 'nowrap',
          overflow: 'hidden',
        }}
      >
        {navigated ? (
          <Glyph d="M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM12 11v6M12 7.5v.5" size={15} color={C.soft} width={1.8} />
        ) : (
          <Glyph d="M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14zM20 20l-4-4" size={15} color={C.faint} width={1.8} />
        )}
        {navigated ? (
          <span>
            {HOST}
            <span style={{color: C.faint}}>{route}</span>
          </span>
        ) : t < T.addrClick ? (
          <span style={{color: C.faint}}>Search or enter address</span>
        ) : (
          <span>
            {typed}
            {caretBlink(t, typed ? T.typeEnd : T.addrClick) || (t >= T.typeStart && t < T.typeEnd) ? (
              <span style={{display: 'inline-block', width: 1.5, height: 17, background: C.ink, verticalAlign: -3, marginLeft: 1}} />
            ) : null}
          </span>
        )}
      </div>
      <div style={{display: 'flex', alignItems: 'center', gap: 10, paddingLeft: 6}}>
        {dlShown ? (
          <div style={{position: 'relative', width: 30, height: 30, transform: `scale(${dlPop})`, display: 'grid', placeItems: 'center'}}>
            <Ring p={dlP} size={30} done={done} />
            <Glyph d={done ? 'M6 12.5l4 4 8-9' : 'M12 6v10M8 12l4 4 4-4'} size={16} color={done ? '#2f9e5b' : C.accent} width={2.2} />
          </div>
        ) : null}
        <Glyph d="M12 5.5v.01M12 12v.01M12 18.5v.01" size={20} color={C.soft} width={2.6} />
      </div>
    </div>
  );
};

// The download bubble under the toolbar button.
const DownloadBubble: React.FC<{t: number; width: number}> = ({t, width}) => {
  if (t < T.downloadStart + 0.05) return null;
  const p = pop(t, T.downloadStart + 0.05, 0.3);
  const q = SMOOTH(prog(t, T.downloadStart, T.downloadDone));
  const done = t >= T.downloadDone;
  const kb = Math.round(DOWNLOADED_FILE.bytes / 1024);
  const got = Math.round(kb * q);
  return (
    <div
      style={{
        position: 'absolute',
        right: 30,
        top: CHROME_H - 4,
        width: 268,
        padding: '12px 14px',
        borderRadius: 12,
        background: '#fff',
        boxShadow: '0 10px 30px rgba(0,0,0,.18), 0 0 0 0.5px rgba(0,0,0,.12)',
        display: 'flex',
        gap: 12,
        alignItems: 'center',
        font: `13px ${UI_FONT}`,
        color: C.ink,
        opacity: p.opacity,
        transform: p.transform,
        transformOrigin: '90% 0',
        zIndex: 5,
      }}
    >
      <div style={{position: 'relative', width: 38, height: 38, flex: 'none', display: 'grid', placeItems: 'center'}}>
        <Ring p={q} size={38} stroke={3} done={done} />
        <Glyph d={done ? 'M6 12.5l4 4 8-9' : 'M12 6v10M8 12l4 4 4-4'} size={18} color={done ? '#2f9e5b' : C.accent} width={2.2} />
      </div>
      <div style={{minWidth: 0}}>
        <div style={{fontWeight: 600, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis'}}>{DOWNLOADED_FILE.name}</div>
        <div style={{color: C.soft, fontSize: 12, marginTop: 2}}>{done ? `${kb} KB · Done` : `${got} KB of ${kb} KB`}</div>
      </div>
    </div>
  );
};

// --- The cursor ---------------------------------------------------------------------
const ARROW = 'M1 1 L1 18.2 L5.3 14.1 L8.2 20.6 L11 19.4 L8.2 13 L14 13 Z';

const Arrow: React.FC<{x: number; y: number; scale: number; plus?: number}> = ({x, y, scale, plus = 0}) => (
  <div style={{position: 'absolute', left: x, top: y, width: 0, height: 0, zIndex: 20}}>
    <svg
      width={24}
      height={26}
      viewBox="0 0 16 22"
      style={{position: 'absolute', left: -1.5, top: -1.5, overflow: 'visible', transform: `scale(${scale * 1.18})`, transformOrigin: '1.5px 1.5px', filter: 'drop-shadow(0 2px 2.5px rgba(0,0,0,.35))'}}
    >
      <path d={ARROW} fill="#fff" stroke="#111" strokeWidth={1.15} strokeLinejoin="round" />
    </svg>
    {plus > 0 ? (
      <div
        style={{
          position: 'absolute',
          left: 14,
          top: 20,
          width: 18,
          height: 18,
          borderRadius: 9,
          background: '#2fb35a',
          border: '1.5px solid #fff',
          boxShadow: '0 1px 3px rgba(0,0,0,.3)',
          display: 'grid',
          placeItems: 'center',
          transform: `scale(${plus})`,
        }}
      >
        <Glyph d="M12 6v12M6 12h12" size={12} color="#fff" width={3.2} />
      </div>
    ) : null}
  </div>
);

const Ripples: React.FC<{t: number; w: number; h: number}> = ({t, w, h}) => (
  <>
    {CLICKS.concat([{t: T.dropped, key: 'drop'}]).map((c) => {
      if (t < c.t || t > c.t + 0.45) return null;
      const p = EASE(prog(t, c.t, c.t + 0.45));
      const at = cursorAt(c.t, w, h);
      const r = mix(4, 24, p);
      return (
        <div
          key={c.key + c.t}
          style={{
            position: 'absolute',
            left: at.x - r,
            top: at.y - r,
            width: r * 2,
            height: r * 2,
            borderRadius: '50%',
            border: '2px solid rgba(28,147,194,.7)',
            background: 'rgba(28,147,194,.12)',
            opacity: 1 - p,
            zIndex: 19,
          }}
        />
      );
    })}
  </>
);

// The page's file badge, small, for the cards being dragged.
const KIND_BG: Record<string, string> = {cia: '#1c93c2', '3dsx': '#3fa65a', zip: '#b07a2a', img: '#c0569a', txt: '#8c8466', rom: '#e69614', file: '#aca27a', media: '#7a5cc0'};
const MiniBadge: React.FC<{name: string}> = ({name}) => (
  <div
    style={{
      width: 32,
      height: 32,
      borderRadius: 8,
      flex: 'none',
      display: 'grid',
      placeItems: 'center',
      background: KIND_BG[kindOf(ext(name))],
      color: '#fff',
      font: `800 9px ${UI_FONT}`,
      boxShadow: 'inset 0 -2px 0 #0002',
    }}
  >
    {ext(name).slice(0, 4).toUpperCase()}
  </div>
);

// The three files being dragged in, trailing the cursor.
const DragCards: React.FC<{t: number; w: number; h: number}> = ({t, w, h}) => {
  if (t < T.dragEnter || t > T.dropped + 0.22) return null;
  const drop = EASE(prog(t, T.dropped, T.dropped + 0.22));
  return (
    <>
      {UP.map((u, i) => {
        const lag = 0.01 * (i + 1);
        const at = cursorAt(Math.max(T.dragEnter, Math.min(t, T.dropped) - lag), w, h);
        const tip = cursorAt(Math.min(t, T.dropped), w, h);
        const x = mix(at.x + 16 + i * 7, tip.x, drop);
        const y = mix(at.y + 22 + i * 9, tip.y, drop);
        const rot = [-5, 3, -1.5][i];
        return (
          <div
            key={u.name}
            style={{
              position: 'absolute',
              left: x,
              top: y,
              width: 168,
              height: 44,
              borderRadius: 10,
              background: 'rgba(255,255,255,.96)',
              boxShadow: '0 6px 16px rgba(0,0,0,.22), 0 0 0 0.5px rgba(0,0,0,.15)',
              display: 'flex',
              alignItems: 'center',
              gap: 8,
              padding: '0 10px 0 6px',
              font: `600 13px ${UI_FONT}`,
              color: '#141414',
              transform: `rotate(${rot * (1 - drop)}deg) scale(${1 - 0.55 * drop})`,
              transformOrigin: '0 0',
              opacity: 1 - drop,
              zIndex: 18 - i,
            }}
          >
            <MiniBadge name={u.name} />
            <span style={{whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis'}}>{u.name}</span>
          </div>
        );
      })}
      {(() => {
        const at = cursorAt(Math.max(T.dragEnter, Math.min(t, T.dropped) - 0.01), w, h);
        return (
          <div
            style={{
              position: 'absolute',
              left: at.x + 16 + 168 - 12,
              top: at.y + 22 - 10,
              minWidth: 22,
              height: 22,
              borderRadius: 11,
              background: '#e5432f',
              color: '#fff',
              font: `700 13px ${UI_FONT}`,
              display: 'grid',
              placeItems: 'center',
              boxShadow: '0 1px 3px rgba(0,0,0,.3)',
              opacity: 1 - drop,
              zIndex: 21,
            }}
          >
            {UP.length}
          </div>
        );
      })()}
    </>
  );
};

// Which row the pointer is over, for the page's hover tint.
const rowUnder = (t: number, w: number, h: number, x: number, y: number): string | null => {
  if (t < T.filesShown || (t >= T.dragEnter && t < T.dropped + 0.05)) return null;
  if ((t >= T.rowClick && t < T.installClick + 0.05) || (t >= T.fileClick && t < T.downloadClick + 0.05)) return null;
  const vh = h - CHROME_H;
  const cl = contentLeft(w);
  if (x < cl || x > cl + contentW(w) || y < CHROME_H) return null;
  const py = y - CHROME_H + scrollAt(t, vh);
  const f = folderAt(t);
  const i = Math.floor((py - rowTop(t, 0)) / M.rowH);
  if (i < 0 || i >= f.entries.length) return null;
  return 'row:' + (f.path === '/' ? '/' : f.path + '/') + f.entries[i].name;
};

export const BrowserWindow: React.FC<{t: number; width?: number; height?: number}> = ({t, width = 1280, height = 800}) => {
  const vh = height - CHROME_H;
  const cur = cursorAt(t, width, height);
  const hover = hoverKey(t, width, height) || rowUnder(t, width, height, cur.x, cur.y);
  const press = pressKey(t);
  const pageIn = EASE(prog(t, T.pageOpens, T.pageOpens + 0.22));
  const loadBar = t >= T.enter && t < T.pageOpens + 0.3;
  const barP = t < T.pageOpens ? 0.15 + 0.6 * IN_OUT(prog(t, T.enter, T.pageOpens)) : 1;
  const barO = 1 - prog(t, T.pageOpens + 0.05, T.pageOpens + 0.3);
  const dragging = t >= T.dragEnter && t < T.dropped;
  const plus = dragging ? SPRING(prog(t, T.overWindow, T.overWindow + 0.2)) : 0;
  return (
    <div style={{position: 'relative', width, height}}>
      <div
        style={{
          position: 'absolute',
          inset: 0,
          borderRadius: 12,
          overflow: 'hidden',
          background: '#fff',
          boxShadow: '0 40px 90px rgba(30,25,10,.28), 0 12px 28px rgba(30,25,10,.18), 0 0 0 1px rgba(0,0,0,.14)',
          isolation: 'isolate',
        }}
      >
        <TabStrip t={t} />
        <Toolbar t={t} width={width} />
        {loadBar ? (
          <div style={{position: 'absolute', left: 0, top: CHROME_H - 2, height: 2.5, width: `${barP * 100}%`, background: C.accent, opacity: barO, zIndex: 3}} />
        ) : null}
        <div style={{position: 'absolute', left: 0, right: 0, top: CHROME_H, height: vh, overflow: 'hidden', background: '#fff'}}>
          {t >= T.pageOpens ? (
            <div style={{position: 'absolute', inset: 0, opacity: pageIn}}>
              <Page t={t} width={width} height={vh} ui={{hover, press}} />
            </div>
          ) : null}
        </div>
        <DownloadBubble t={t} width={width} />
      </div>
      {/* The pointer may leave the window (to fetch the files), so it is not clipped. */}
      <Ripples t={t} w={width} h={height} />
      <DragCards t={t} w={width} h={height} />
      <Arrow x={cur.x} y={cur.y} scale={1 - 0.14 * pressAmount(t)} plus={plus} />
    </div>
  );
};
