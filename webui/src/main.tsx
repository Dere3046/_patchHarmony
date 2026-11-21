// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

import 'mdui/mdui.css';
import 'mdui/components/button.js';
import 'mdui/components/chip.js';
import 'mdui/components/dialog.js';
import 'mdui/components/segmented-button-group.js';
import 'mdui/components/segmented-button.js';
import 'mdui/components/switch.js';
import 'mdui/components/tab.js';
import 'mdui/components/tabs.js';
import './styles/base.css';
import './styles/cards.css';
import './styles/hero.css';
import './styles/overview.css';
import './styles/forms.css';

import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>
);
