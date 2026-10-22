import React from 'react';
import {Img, staticFile} from 'remotion';
import {PAGE_CSS} from './pageCss';
import {Badge, Svg} from './icons';
import {EASE, SMOOTH, SPRING, clamp, ext, mix, pop, prog, rise, size} from './anim';
import {
  CIA_META,
  DOWNLOADED_FILE,
  REMOTE_INSTALL,
  SD_FREE,
  SD_TOTAL,
  SENT_LINK,
  SENT_NAME,
  T,
  UP,
  UP_PANEL,
  folderAt,
  upLoaded,
  upPanel,
  type Entry,
} from './script';
import {layout, type Layout} from './geometry';

// The console's page (source/core/remote_page.cpp) rebuilt as React, with its
// own CSS, showing what it shows at time t. Animations the page runs with CSS
// keyframes are computed from t here instead.

export type UI = {hover?: string | null; press?: string | null};

const EXTRA_CSS = `
.dsweb{position:absolute;inset:0;overflow:hidden;font:16px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif;color:var(--ink);-webkit-font-smoothing:antialiased}
.dsweb *,.dsweb *::before,.dsweb *::after{animation:none!important;transition:none!important}
.dsweb .dsbody{position:absolute;left:0;right:0;top:0;will-change:transform}
.dsweb .ups{position:relative;top:0}
.dsweb .spin,.dsweb .reload.busy .i{transform:rotate(var(--spin))}
.dsweb .pill[data-state=installing] .dot,.dsweb .pill[data-state=downloading] .dot,.dsweb .pill[data-state=sending] .dot,.dsweb .pill[data-state=busy] .dot{transform:rotate(var(--spin))}
.dsweb .btn.busy::after{transform:rotate(var(--spin7))}
.dsweb .pill[data-state=connecting] .dot{opacity:var(--pulse)}
.dsweb .pill[data-state=choosing] .dot,.dsweb .pill[data-state=asking] .dot{box-shadow:0 0 0 var(--ringS) rgba(230,150,20,var(--ringA))}
.dsweb .active .fill,.dsweb .uphead .fill{background-position:var(--stripe) 0,0 0}
.dsweb .ups.done .uphead .fill{background-position:0 0}
.dsweb .loadline::after{left:var(--slide)}
.dsweb .card[data-state=choosing] .art{transform:translateY(var(--bob))}
.dsweb .dsta{white-space:pre-wrap;overflow-wrap:anywhere;position:relative}
.dsweb .dsta .ph{color:#8b8b86}
.dsweb .dsta .caret{display:inline-block;width:1.5px;height:1.15em;margin:0 -1.5px -0.2em 0;background:#141414;vertical-align:baseline}
.dsweb .dsta .grip{position:absolute;right:2px;bottom:2px;width:9px;height:9px}
.dsweb .dsdialog{position:absolute;left:50%;top:50%;margin:0;z-index:51;overflow:hidden}
.dsweb .dsbackdrop{position:absolute;inset:0;z-index:50}
.dsweb .ic{font-family:system-ui,sans-serif}
`;

// --- Status ---------------------------------------------------------------------
// SAYS / HEADS and the pill states from remote_page.cpp; the texts after a
// request are the console's own (App::receive in source/app/queue.cpp).
type Status = {state: string; text: string};

const statusAt = (t: number): Status => {
  if (t < T.ready) return {state: 'connecting', text: 'Connecting...'};
  if (t >= T.sent && t < T.linkDownloading) return {state: 'choosing', text: 'Waiting for you to choose Download on the 3DS.'};
  if (t >= T.linkDownloading && t < T.linkDone) return {state: 'downloading', text: 'Downloading 1 link.'};
  if (t >= UP[0].start && t < UP[UP.length - 1].end) return {state: 'sending', text: 'Sending to the 3DS · ' + upPanel(t).pct + '%'};
  if (t >= T.installSent && t < T.installing) return {state: 'choosing', text: 'Waiting for you to choose Install on the 3DS.'};
  if (t >= T.installing && t < T.installDone) return {state: 'installing', text: 'Installing ' + REMOTE_INSTALL + '.'};
  return {state: 'ready', text: '3DS ready'};
};

const HEADS: Record<string, string> = {choosing: 'Over to you on the 3DS', installing: 'Installing on the 3DS'};

// --- Toasts -----------------------------------------------------------------------
type Toast = {at: number; kind: '' | 'good' | 'bad'; text: string; dur?: number};
const TOASTS: Toast[] = [
  // Shorter than the page's 3.8 s so it clears before the upload panel sticks.
  {at: T.linkDone, kind: 'good', text: 'Done with 1 link on the 3DS. History has how each went.', dur: 2.3},
  {at: T.installSent, kind: '', text: 'Sent to the 3DS. Choose Install on the console.'},
  {at: T.installDone, kind: 'good', text: 'Installed ' + REMOTE_INSTALL + '.'},
];

// --- Pieces -----------------------------------------------------------------------
const cls = (...xs: (string | false | null | undefined)[]) => xs.filter(Boolean).join(' ');

const Toasts: React.FC<{t: number}> = ({t}) => (
  <div className="toasts">
    {TOASTS.filter((x) => t >= x.at && t < x.at + (x.dur ?? 3.8) + 0.32).map((x) => {
      const inP = SPRING(prog(t, x.at, x.at + 0.35));
      const inO = EASE(prog(t, x.at, x.at + 0.35));
      const out = EASE(prog(t, x.at + (x.dur ?? 3.8), x.at + (x.dur ?? 3.8) + 0.3));
      const style: React.CSSProperties = {
        opacity: inO * (1 - out),
        transform: `translateY(${(1 - inP) * -14 + out * -10}px) scale(${(0.96 + 0.04 * inP) * (1 - 0.03 * out)})`,
      };
      return (
        <div key={x.at} className={cls('toast', x.kind)} style={style}>
          <Svg name={x.kind === 'bad' ? 'warn' : x.kind === 'good' ? 'check' : 'info'} />
          {x.text}
        </div>
      );
    })}
  </div>
);

const Row: React.FC<{
  e: Entry;
  path: string;
  ui: UI;
  t: number;
  anim?: {at: number; i: number};
  freshAt?: number;
}> = ({e, path, ui, t, anim, freshAt}) => {
  const key = 'row:' + path;
  let style: React.CSSProperties = {};
  if (anim) style = rise(t, anim.at + Math.min(anim.i, 14) * 0.016, 0.3);
  if (freshAt !== undefined && t >= freshAt && t < freshAt + 1.8) {
    const p = (t - freshAt) / 1.8;
    const a = p < 0.3 ? 1 : 1 - SMOOTH((p - 0.3) / 0.7);
    style = {...style, background: `rgba(191,232,200,${a})`};
  }
  const meta = [e.dir ? 'Folder' : size(e.bytes || 0), e.time].filter(Boolean).join(' · ');
  return (
    <li className={cls('row', ui.hover === key && 'hov')} style={style} data-k={key}>
      <input type="checkbox" readOnly tabIndex={-1} />
      <Badge name={e.name} dir={e.dir} />
      <div className="what">
        <span className="name">{e.name}</span>
        <span className="meta">{meta}</span>
      </div>
      <button className="more" tabIndex={-1}>
        <Svg name="more" />
      </button>
    </li>
  );
};

// --- The details sheet -------------------------------------------------------------
type SheetSpec = {open: number; close: number; entry: Entry; where: string; meta?: number};

const sheetAt = (t: number): SheetSpec | null => {
  if (t >= T.rowClick + 0.03 && t < T.installClick + 0.04)
    return {
      open: T.rowClick + 0.03,
      close: T.installClick + 0.04,
      entry: {name: REMOTE_INSTALL, bytes: UP.find((u) => u.name === REMOTE_INSTALL)!.bytes, time: '2026-10-24 16:21'},
      where: '/',
      meta: T.metaLoaded,
    };
  if (t >= T.fileClick + 0.03 && t < T.downloadClick + 0.04)
    return {
      open: T.fileClick + 0.03,
      close: T.downloadClick + 0.04,
      entry: {name: DOWNLOADED_FILE.name, bytes: DOWNLOADED_FILE.bytes, time: '2026-10-24 16:02'},
      where: DOWNLOADED_FILE.folder.replace(/^sdmc:/, '').replace(/\/$/, ''),
    };
  return null;
};

export const sheetActions = (name: string) => {
  const x = ext(name);
  const acts: [string, Parameters<typeof Svg>[0]['name'], string?][] = [['Download', 'download']];
  if (['png', 'jpg', 'jpeg', 'gif', 'bmp', 'webp'].includes(x)) acts.push(['View', 'eye']);
  if (x === 'cia') acts.push(['Install on 3DS', 'install']);
  acts.push(['Rename', 'rename'], ['Move To...', 'move'], ['Copy Path', 'copy'], ['Delete', 'trash', ' red']);
  return acts;
};

const Sheet: React.FC<{t: number; s: SheetSpec; ui: UI}> = ({t, s, ui}) => {
  const e = s.entry;
  const x = ext(e.name);
  const p = pop(t, s.open);
  const backdrop = EASE(prog(t, s.open, s.open + 0.2));
  const metaIn = s.meta !== undefined && t >= s.meta;
  const loadingMeta = s.meta !== undefined && !metaIn;
  const facts: [string, string][] = [
    ['Size', size(e.bytes || 0) + ((e.bytes || 0) >= 1024 ? ' (' + (e.bytes || 0).toLocaleString('en-US') + ' bytes)' : '')],
    ['Changed', e.time],
    ['Where', s.where || '/'],
  ];
  const metaRows: [string, string][] = metaIn
    ? [
        ['Title', CIA_META.name],
        ['Publisher', CIA_META.publisher],
        ['Title ID', CIA_META.titleId],
        ['Version', CIA_META.version],
      ]
    : [];
  const sub = metaIn ? CIA_META.name + ' · ' + CIA_META.publisher : x ? x.toUpperCase() + ' file' : 'File';
  return (
    <>
      <div className="dsbackdrop" style={{opacity: backdrop}} />
      <div
        className="dsdialog sheet"
        data-k="sheet"
        style={{opacity: p.opacity, transform: `translate(-50%,-50%) ${p.transform}`}}
      >
        <button className="close" tabIndex={-1}>
          <Svg name="x" />
        </button>
        <div className="top">
          <Badge name={e.name} />
          <div>
            <h2>{e.name}</h2>
            <div className="sub">{sub}</div>
          </div>
        </div>
        <dl className="facts">
          {loadingMeta ? (
            <>
              <div className="skel">
                <b style={{['--w' as string]: '70%', ...shimmer(t)}} />
              </div>
              <div className="skel">
                <b style={{['--w' as string]: '45%', ...shimmer(t)}} />
              </div>
            </>
          ) : null}
          {[...metaRows, ...facts].map(([k, v]) => (
            <React.Fragment key={k}>
              <dt>{k}</dt>
              <dd>{v}</dd>
            </React.Fragment>
          ))}
        </dl>
        <div className="acts">
          {sheetActions(e.name).map(([label, icon, extra]) => {
            const key = 'act:' + label;
            return (
              <button
                key={label}
                tabIndex={-1}
                data-k={key}
                className={cls('btn' + (extra || ''), ui.hover === key && 'hov', ui.press === key && 'press')}
              >
                <Svg name={icon} />
                {label}
              </button>
            );
          })}
        </div>
      </div>
    </>
  );
};

const shimmer = (t: number): React.CSSProperties => ({backgroundPosition: `${200 - ((t / 1.2) % 1) * 400}% 0`});

// --- The page ----------------------------------------------------------------------
export const Page: React.FC<{t: number; width: number; height: number; ui: UI}> = ({t, width, height, ui}) => {
  const L: Layout = layout(t, width, height);
  const st = statusAt(t);
  const onFiles = t >= T.filesClick;
  const vars = {
    '--spin': `${(t / 0.8) * 360}deg`,
    '--spin7': `${(t / 0.7) * 360}deg`,
    '--pulse': String(1 - 0.65 * (0.5 - 0.5 * Math.cos(((t % 1.2) / 1.2) * Math.PI * 2))),
    '--ringS': `${Math.min(1, ((t % 1.4) / 1.4) / 0.7) * 7}px`,
    '--ringA': String(0.4 * (1 - Math.min(1, ((t % 1.4) / 1.4) / 0.7))),
    '--stripe': `${((t / 0.8) % 1) * 18}px`,
    '--slide': `${mix(-40, 100, EASE((t % 1) / 1))}%`,
    '--bob': `${-3 * (0.5 - 0.5 * Math.cos(((t % 1.6) / 1.6) * Math.PI * 2))}px`,
  } as React.CSSProperties;

  // Tabs: the ink slides over .3s with --ease.
  const inkP = EASE(prog(t, T.filesClick, T.filesClick + 0.3));
  const inkLeft = 3 + inkP * L.tabW;

  return (
    <div className="dsweb" style={vars}>
      <style>{PAGE_CSS + EXTRA_CSS}</style>
      <div className="dsbody" style={{transform: `translateY(${-L.scroll}px)`, minHeight: height + L.scroll}}>
        <main>
          <header className="head">
            <div className="brand">
              <a className="logo">
                <Img src={staticFile('brand/icon-256.png')} alt="" />
                <h1>3dsDL</h1>
              </a>
              <nav className="ext">
                <a>
                  Website
                  <Svg name="out" />
                </a>
                <a>
                  GitHub
                  <Svg name="out" />
                </a>
              </nav>
            </div>
            <div className="pill" data-state={st.state} data-k="pill">
              <i className="dot" />
              <span>{st.text}</span>
            </div>
          </header>
          <nav className="tabs">
            <span className="ink" style={{left: inkLeft, width: L.tabW}} />
            <a className={onFiles ? '' : 'on'}>Links</a>
            <a className={onFiles ? 'on' : ''} data-k="tab-files">
              Files
            </a>
          </nav>
          {onFiles ? <Files t={t} ui={ui} L={L} st={st} /> : <Links t={t} ui={ui} st={st} />}
        </main>
      </div>
      {t >= T.overWindow && t < T.dropped ? <DropOverlay t={t} /> : null}
      <Toasts t={t} />
      {(() => {
        const s = sheetAt(t);
        return s ? <Sheet t={t} s={s} ui={ui} /> : null;
      })()}
    </div>
  );
};

// --- Links -------------------------------------------------------------------------
const Links: React.FC<{t: number; ui: UI; st: Status}> = ({t, ui, st}) => {
  const focused = t < T.sendClick;
  const pasted = t >= T.linkPasted && t < T.sent;
  const caretOn = focused && ((t - (pasted ? T.linkPasted : T.boxClick)) % 1.06 + 1.06) % 1.06 < 0.53;
  const busy = t >= T.sendClick && t < T.sent;
  const sent = t >= T.sent;
  const done = t >= T.linkDone;
  const liText = done ? 'Done with 1 link on the 3DS. History has how each went.' : st.state === 'ready' ? 'Sent to the 3DS.' : st.text;
  return (
    <section>
      <p>Paste one or more direct download links. They go to the 3DS, which downloads them one after another.</p>
      <form className="panel" onSubmit={(e) => e.preventDefault()}>
        <div className={cls('dsta', focused && 'foc')} data-k="box">
          {pasted ? (
            <>
              {SENT_LINK}
              {caretOn ? <span className="caret" /> : null}
            </>
          ) : (
            <>
              {caretOn ? <span className="caret" /> : null}
              <span className="ph">https://...</span>
            </>
          )}
          <svg className="grip" viewBox="0 0 9 9" aria-hidden="true">
            <path d="M8 2 2 8M8 5.5 5.5 8" stroke="#9a9384" strokeWidth="1" strokeLinecap="round" />
          </svg>
        </div>
        <div className="formrow">
          <span className={cls('count', pasted && 'has')}>{pasted ? '1 link ready' : ''}</span>
          <button
            className={cls('btn primary', busy && 'busy', ui.hover === 'send' && 'hov', ui.press === 'send' && 'press')}
            type="button"
            tabIndex={-1}
            data-k="send"
          >
            Send to 3DS
          </button>
        </div>
      </form>
      <ul className="sent">
        {sent ? (
          <li className={done ? 'ok' : 'wait'} style={rise(t, T.sent)}>
            <Svg name={done ? 'check' : 'link'} />
            <div>
              <b>1 link: {SENT_NAME}</b>
              <span>{liText}</span>
            </div>
          </li>
        ) : null}
      </ul>
      <small>Only links are sent. Keep 3dsDL on the Receive screen while you send.</small>
    </section>
  );
};

// --- Files -------------------------------------------------------------------------
const Files: React.FC<{t: number; ui: UI; L: Layout; st: Status}> = ({t, ui, L, st}) => {
  if (t < T.filesShown) {
    return (
      <section>
        <div className="panel gate" style={rise(t, T.filesClick + 0.02, 0.3)}>
          <div className="art" style={{transform: `translateY(${-3 * (0.5 - 0.5 * Math.cos(((t % 1.8) / 1.8) * Math.PI * 2))}px)`}}>
            <Svg name="console" />
          </div>
          <h2>Connecting to the 3DS</h2>
          <div className="waiting">
            <span className="spin" />
            One moment
          </div>
        </div>
      </section>
    );
  }
  const f = folderAt(t);
  // Loading: the line and the dimmed list while a listing is on its way.
  const loads: [number, number][] = [
    [T.filesClick, T.filesShown],
    [T.lumaClick, T.lumaShown],
    [T.shotsClick, T.shotsShown],
    [T.installDone, T.installDone + 0.1],
    ...UP.map((u) => [u.end, u.shown] as [number, number]),
  ];
  const loading = loads.some(([a, b]) => t >= a && t < b);
  // Moving to another folder dims the old list; the quiet reloads after each
  // upload only show the line.
  const dim = (t >= T.lumaClick && t < T.lumaShown) || (t >= T.shotsClick && t < T.shotsShown);
  const showCard = st.state === 'choosing' || st.state === 'installing';
  const cardSince = st.state === 'choosing' ? T.installSent : T.installing;
  const used = (SD_TOTAL - SD_FREE) / SD_TOTAL;
  const showUps = t >= UP_PANEL && t < T.clearClick;
  const panel = upPanel(t);
  return (
    <section>
      <div>
        {showCard ? (
          <div className="card" data-state={st.state} style={rise(t, cardSince)}>
            <div className="art">{st.state === 'installing' ? <span className="spin" /> : <Svg name="console" />}</div>
            <div>
              <b>{HEADS[st.state]}</b>
              <span>{st.text}</span>
            </div>
          </div>
        ) : null}
        <div className="storage">
          <div className="meter">
            <i style={{width: (used * 100).toFixed(1) + '%'}} />
          </div>
          <span>
            {size(SD_FREE)} free of {size(SD_TOTAL)}
          </span>
        </div>
        <div className="pathbar">
          <button className="btn icon-only" disabled={f.path === '/'} tabIndex={-1}>
            <Svg name="up" />
          </button>
          <nav className="crumbs">
            <a>SD</a>
            {f.parts.map((p) => (
              <React.Fragment key={p}>
                <span>/</span>
                <a>{p}</a>
              </React.Fragment>
            ))}
          </nav>
        </div>
        <div className="tools">
          <label className="btn primary">
            <Svg name="upload" />
            Upload
          </label>
          <label className="btn">
            <Svg name="folder" />
            Folder
          </label>
          <button className="btn" tabIndex={-1}>
            <Svg name="folderplus" />
            New Folder
          </button>
          <button className={cls('btn icon-only reload', loading && 'busy')} tabIndex={-1}>
            <Svg name="refresh" />
          </button>
        </div>
        <div className="tools2">
          <label className="search">
            <Svg name="search" />
            <input type="search" placeholder="Filter this folder" readOnly tabIndex={-1} />
          </label>
          <select aria-label="Sort" tabIndex={-1} defaultValue="name">
            <option value="name">By name</option>
            <option value="size">By size</option>
            <option value="time">By date</option>
            <option value="type">By type</option>
          </select>
          <label className="check">
            <input type="checkbox" readOnly tabIndex={-1} />
            Hidden
          </label>
        </div>
        {showUps ? (
          <div
            className={cls('ups panel', panel.all && 'done', L.stuck > 0.5 && 'stuck')}
            data-k="ups"
            style={{...rise(t, UP_PANEL, 0.3), ...(L.stuck > 0 ? {transform: `translateY(${L.stuck}px)`} : {})}}
          >
            <div className="uphead">
              <div>
                <b>{panel.title}</b>
                <span>{panel.sub}</span>
              </div>
              <div className="upbtns">
                <button className="btn small toggle" tabIndex={-1}>
                  <Svg name="chevron" />
                  Details ({UP.length})
                </button>
                {!panel.all ? (
                  <button className="btn small" tabIndex={-1}>
                    Cancel All
                  </button>
                ) : null}
                {panel.finished > 0 ? (
                  <button
                    className={cls('btn small', ui.hover === 'clear' && 'hov', ui.press === 'clear' && 'press')}
                    tabIndex={-1}
                    data-k="clear"
                  >
                    Clear Finished
                  </button>
                ) : null}
              </div>
              <div className="track">
                <div className="fill" style={{width: panel.pct + '%'}} />
              </div>
            </div>
          </div>
        ) : null}
        <div className="listwrap">
          <div className={cls('loadline', loading && 'on')} />
          <div className="listhead">
            <input type="checkbox" readOnly tabIndex={-1} />
            <span>
              {f.entries.length} {f.entries.length === 1 ? 'item' : 'items'}
            </span>
          </div>
          <ul className={cls('list', dim && 'dim')}>
            {f.entries.map((e, i) => {
              const path = (f.path === '/' ? '/' : f.path + '/') + e.name;
              return (
                <Row
                  key={f.path + e.name}
                  e={e}
                  path={path}
                  ui={ui}
                  t={t}
                  anim={{at: f.since, i}}
                  freshAt={e.shown}
                />
              );
            })}
          </ul>
        </div>
        <small className="hint">
          Drop files or folders anywhere on this page to upload them here.{' '}
          <span className="keys">
            <kbd>/</kbd> filter, <kbd>Backspace</kbd> up, <kbd>Del</kbd> delete, <kbd>F2</kbd> rename, <kbd>Ctrl</kbd>+<kbd>A</kbd> select all.
          </span>
        </small>
      </div>
    </section>
  );
};

const DropOverlay: React.FC<{t: number}> = ({t}) => {
  const fade = clamp(prog(t, T.overWindow, T.overWindow + 0.15));
  const p = pop(t, T.overWindow, 0.3);
  return (
    <div className="drop" style={{opacity: fade}}>
      <div style={{opacity: p.opacity, transform: p.transform}}>
        <Svg name="upload" />
        <b>Drop to upload</b>
        <span>into the SD card</span>
      </div>
    </div>
  );
};

// Exposed for the cursor and the toolbar.
export {statusAt, upLoaded};
