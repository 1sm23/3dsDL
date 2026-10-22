// Renders 3dsDL's animated screens as video clips. The screen engine
// (engine/screens) redraws the real app UI on
// canvases from docs/DESIGN.md and the app's icon atlas, and animates it:
// the focus ring glides, buttons press, progress bars fill. Here each
// sequence is drawn at 6 canvas pixels per console pixel (sharp in the 4K
// master), top screen (2400 x 1440) beside the bottom screen (1920 x 1440), and
// render-clips.sh crops the two apart.
import React, {useEffect, useRef} from 'react';
import {AbsoluteFill, Composition, continueRender, delayRender, registerRoot, useCurrentFrame, useVideoConfig} from 'remotion';
import {drawSequence, loadScreenAssets, SEQ_IDS, SEQUENCES} from './engine/screens/index';
import type {SeqId} from './engine/screens/index';
import {BrowserWindow} from './engine/web';
import {Ready} from './engine/lib/Ready';

const S = 6;
const FPS = 60;
const TAIL = 0.5; // seconds held on the last frame

const Clip: React.FC<{seq: SeqId}> = ({seq}) => {
  const frame = useCurrentFrame();
  const {fps} = useVideoConfig();
  const top = useRef<HTMLCanvasElement>(null);
  const bottom = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const handle = delayRender(`${seq} ${frame}`);
    loadScreenAssets().then(() => {
      drawSequence(seq, frame / fps, top.current!.getContext('2d')!, bottom.current!.getContext('2d')!, S);
      continueRender(handle);
    });
  }, [seq, frame, fps]);
  return (
    <AbsoluteFill style={{background: '#000'}}>
      <canvas ref={top} width={400 * S} height={240 * S} style={{position: 'absolute', left: 0, top: 0}} />
      <canvas ref={bottom} width={320 * S} height={240 * S} style={{position: 'absolute', left: 400 * S, top: 0}} />
    </AbsoluteFill>
  );
};

// One moment of a sequence as a large still, for the 3D console's screen
// textures (the camera push fills a 4K frame with the top screen).
const Still: React.FC<{seq: SeqId; t: number; s: number}> = ({seq, t, s}) => {
  const top = useRef<HTMLCanvasElement>(null);
  const bottom = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const handle = delayRender(`still ${seq} ${t}`);
    loadScreenAssets().then(() => {
      drawSequence(seq, t, top.current!.getContext('2d')!, bottom.current!.getContext('2d')!, s);
      continueRender(handle);
    });
  }, [seq, t, s]);
  return (
    <AbsoluteFill style={{background: '#000'}}>
      <canvas ref={top} width={400 * s} height={240 * s} style={{position: 'absolute', left: 0, top: 0}} />
      <canvas ref={bottom} width={320 * s} height={240 * s} style={{position: 'absolute', left: 400 * s, top: 0}} />
    </AbsoluteFill>
  );
};

// The 3ds.local page in a browser window, on the same clock as the
// receive sequence, so the console and the browser can play side by side.
const Browser: React.FC = () => {
  const frame = useCurrentFrame();
  const {fps} = useVideoConfig();
  return (
    <AbsoluteFill style={{background: 'transparent'}}>
      <Ready>
        <BrowserWindow t={frame / fps} width={1280} height={800} />
      </Ready>
    </AbsoluteFill>
  );
};

const STILL_S = 16;

const Root: React.FC = () => (
  <>
    <Composition id="still-home" component={Still} width={720 * STILL_S} height={240 * STILL_S} fps={FPS}
      durationInFrames={1} defaultProps={{seq: 'home' as SeqId, t: 0, s: STILL_S}} />
    <Composition id="browser-receive" component={Browser} width={1280} height={800} fps={FPS}
      durationInFrames={Math.ceil((SEQUENCES.receive.duration + TAIL) * FPS)} />
    {SEQ_IDS.map((id) => (
      <Composition key={id} id={`clip-${id}`} component={Clip} width={720 * S} height={240 * S} fps={FPS}
        durationInFrames={Math.ceil((SEQUENCES[id].duration + TAIL) * FPS)} defaultProps={{seq: id}} />
    ))}
  </>
);

registerRoot(Root);
