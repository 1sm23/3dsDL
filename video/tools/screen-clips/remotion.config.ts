import {Config} from '@remotion/cli/config';
import path from 'node:path';
import webpack from 'webpack';

// Use patches/data.ts in place of engine/screens/data.ts: the film's copy
// changes stay in one small file.
Config.overrideWebpackConfig((config) => ({
  ...config,
  plugins: [
    ...(config.plugins ?? []),
    new webpack.NormalModuleReplacementPlugin(/^\.\/data(\.ts)?$/, (resource) => {
      if (resource.context && resource.context.includes(`${path.sep}engine${path.sep}screens`)) {
        resource.request = path.resolve(process.cwd(), 'patches/data.ts');
      }
    }),
  ],
}));
