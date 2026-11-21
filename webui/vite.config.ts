// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

const VALID_FLAVORS = new Set(['auto', 'standard', 'native', 'main-native']);
const rawFlavor = (process.env.VITE_PHDROID_FLAVOR || 'auto').trim();
const flavor = VALID_FLAVORS.has(rawFlavor) ? rawFlavor : 'auto';
const outDir = (process.env.VITE_OUT_DIR || 'dist').trim() || 'dist';

export default defineConfig({
  plugins: [react()],
  base: './',
  define: {
    __PHDROID_FLAVOR__: JSON.stringify(flavor)
  },
  build: {
    outDir,
    emptyOutDir: true
  }
});
