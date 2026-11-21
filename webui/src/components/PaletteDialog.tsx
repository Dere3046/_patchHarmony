// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

import { useEffect, useState } from 'react';
import { DEFAULT_SEED_COLOR, getLangStrings, PALETTE_PRESETS } from '../i18n';
import type { Lang } from '../types';
import { MduiDialog } from './MduiControls';

interface PaletteDialogProps {
  open: boolean;
  lang: Lang;
  seedColor: string;
  onSelectColor: (hex: string) => void;
  onClose: () => void;
}

function normalizeHex(input: string): string | null {
  const m = input.trim().match(/^#?([0-9a-fA-F]{6})$/);
  return m ? `#${m[1].toLowerCase()}` : null;
}

export function PaletteDialog({
  open,
  lang,
  seedColor,
  onSelectColor,
  onClose
}: PaletteDialogProps) {
  const dict = getLangStrings(lang);
  const [hexDraft, setHexDraft] = useState(seedColor);

  useEffect(() => {
    setHexDraft(seedColor);
  }, [seedColor, open]);

  const applyHex = (raw: string) => {
    setHexDraft(raw);
    const norm = normalizeHex(raw);
    if (norm) onSelectColor(norm);
  };

  return (
    <MduiDialog open={open} headline={dict.paletteTitle} onClose={onClose}>
      <div className="row-desc" style={{ marginTop: 0 }}>
        {dict.paletteDesc}
      </div>

      <div className="swatch-grid">
        {PALETTE_PRESETS.map((preset) => {
          const selected = preset.hex === seedColor.toLowerCase();
          return (
            <button
              type="button"
              key={preset.id}
              className={`swatch-item ${selected ? 'selected' : ''}`}
              onClick={() => onSelectColor(preset.hex)}
            >
              <span className="swatch-circle" style={{ background: preset.hex }}>
                {selected ? '✓' : ''}
              </span>
              <span>{dict.paletteNames[preset.id] || preset.id}</span>
            </button>
          );
        })}
      </div>

      <div className="custom-color-row">
        <span className="row-label" style={{ fontSize: '13px' }}>
          {dict.paletteCustom}
        </span>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <input
            type="text"
            className="text-input"
            style={{ width: '92px', padding: '6px 8px' }}
            value={hexDraft}
            onChange={(e) => applyHex(e.target.value)}
          />
          <input
            type="color"
            className="color-picker-native"
            value={seedColor}
            onChange={(e) => applyHex(e.target.value)}
          />
        </div>
      </div>

      <div className="palette-preview">
        <div
          className="palette-chip-demo"
          style={{
            background: 'rgb(var(--mdui-color-primary))',
            color: 'rgb(var(--mdui-color-on-primary))'
          }}
        >
          Primary
        </div>
        <div
          className="palette-chip-demo"
          style={{
            background: 'rgb(var(--mdui-color-primary-container))',
            color: 'rgb(var(--mdui-color-on-primary-container))'
          }}
        >
          Container
        </div>
        <div
          className="palette-chip-demo"
          style={{
            background: 'rgb(var(--mdui-color-secondary-container))',
            color: 'rgb(var(--mdui-color-on-secondary-container))'
          }}
        >
          Secondary
        </div>
      </div>

      <mdui-button slot="action" variant="text" onClick={() => onSelectColor(DEFAULT_SEED_COLOR)}>
        {dict.resetBtn}
      </mdui-button>
      <mdui-button slot="action" variant="filled" onClick={onClose}>
        {dict.doneBtn}
      </mdui-button>
    </MduiDialog>
  );
}
