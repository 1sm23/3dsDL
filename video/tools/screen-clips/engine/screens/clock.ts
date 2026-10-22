// The status bar's clock for the frame being drawn, set by the timeline.
let clock = '10/20 (Tue)  20:07';

export const setClock = (value: string) => {
  clock = value;
};

export const getClock = () => clock;
