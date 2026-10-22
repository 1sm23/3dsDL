import React, {useEffect, useState} from 'react';
import {continueRender, delayRender} from 'remotion';
import {loadFonts} from './fonts';
import {loadScreenAssets} from '../screens-api';

let ready = false;
let loading: Promise<void> | null = null;

// Holds a composition back until the fonts and the screens' icons and
// pictures are loaded. The screens draw while React renders, so anything
// mounted earlier would paint a frame with missing glyphs and icons.
export const Ready: React.FC<{children: React.ReactNode}> = ({children}) => {
  const [ok, setOk] = useState(ready);
  const [handle] = useState(() => (ready ? null : delayRender('fonts and screen assets')));
  useEffect(() => {
    if (ok) return;
    loading ??= Promise.all([loadFonts(), loadScreenAssets()]).then(() => {
      ready = true;
    });
    loading.then(() => {
      setOk(true);
      if (handle !== null) continueRender(handle);
    });
  }, [ok, handle]);
  return ok ? <>{children}</> : null;
};
