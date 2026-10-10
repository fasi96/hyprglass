# Third-party notices

## Liquid touch

Liquid touch (`src/LiquidSim.hpp`, `src/LiquidSim.cpp`, the `liquid_*.frag`
shaders and the LIQUID TOUCH block of the glass shader in `src/Shaders.hpp`) is
adapted from two MIT-licensed works:

- **"Viscous Liquid - Cursor FX"** by Sabo Sugi —
  https://codepen.io/sabosugi/pen/01a125aa-40e8-70ca-b198-550dc149d263
  (public CodePen pens are MIT-licensed). The idea and the look: a pointer-driven
  fluid whose liquid layer bends the view behind it into a lens, drags it along
  the flow and lets it flow back, with a rainbow split and sharp glints.
- **WebGL Fluid Simulation** by Pavel Dobryakov —
  https://github.com/PavelDoGreat/WebGL-Fluid-Simulation. The stable-fluids
  solver passes (advection, curl, vorticity confinement, divergence, pressure,
  gradient subtraction) the pen builds on.

### MIT License — "Viscous Liquid - Cursor FX"

Copyright (c) 2026 Sabo Sugi (https://codepen.io/sabosugi)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

### MIT License — WebGL Fluid Simulation

Copyright (c) 2017 Pavel Dobryakov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
