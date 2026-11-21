// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

import { getLangStrings, PARAMS_SCHEMA } from '../i18n';
import type { ExtraParam, Lang, ParamSpec, PidBackendMask, StatusSnapshot } from '../types';
import { CloseIcon } from './Icons';
import { MduiSegmentedGroup, MduiSwitch } from './MduiControls';

interface ParamsPageProps {
  lang: Lang;
  snapshot: StatusSnapshot;
  busy: boolean;
  selectedKmi: string;
  activeBackends: PidBackendMask;
  effectiveMainDefs: Record<string, string>;
  modMainEnabled: boolean;
  modVendorEnabled: boolean;
  formMain: Record<string, string>;
  formVendor: Record<string, string>;
  extraParams: ExtraParam[];
  previewText: string;
  unsaved: boolean;
  onSelectKmi: (kmi: string) => void;
  onToggleModMain: (v: boolean) => void;
  onToggleModVendor: (v: boolean) => void;
  onChangeMain: (key: string, val: string) => void;
  onChangeVendor: (key: string, val: string) => void;
  onAddExtra: () => void;
  onRemoveExtra: (id: number) => void;
  onUpdateExtra: (id: number, field: 'mod' | 'key' | 'val', value: string) => void;
  onResetDefaults: () => void;
  onInstallOnce: () => void;
  onSaveConfig: () => void;
}

export function ParamsPage({
  lang,
  snapshot,
  busy,
  selectedKmi,
  activeBackends,
  effectiveMainDefs,
  modMainEnabled,
  modVendorEnabled,
  formMain,
  formVendor,
  extraParams,
  previewText,
  unsaved,
  onSelectKmi,
  onToggleModMain,
  onToggleModVendor,
  onChangeMain,
  onChangeVendor,
  onAddExtra,
  onRemoveExtra,
  onUpdateExtra,
  onResetDefaults,
  onInstallOnce,
  onSaveConfig
}: ParamsPageProps) {
  const dict = getLangStrings(lang);
  const zh = lang === 'zh';
  const availCount = snapshot.hasVendorKo ? 2 : 1;
  const availText = zh ? `${availCount} 个可用模块` : `${availCount} available`;
  const multiKmi = snapshot.kmis.length > 1;

  const isOptionDisabled = (item: ParamSpec, optVal: string) => {
    if (item.key !== 'pid_impl') return false;
    if (activeBackends === 'main' && optVal === 'native') return true;
    if (activeBackends === 'native' && optVal === 'main') return true;
    return false;
  };

  const isParamDisabled = (item: ParamSpec) => {
    if (item.key === 'nsproxy') {
      return activeBackends === 'main' || formMain.pid_impl !== 'native';
    }
    return false;
  };

  const paramDesc = (item: ParamSpec) => {
    if (item.key === 'pid_impl' && activeBackends === 'main') {
      return zh ? '当前模块仅编入默认后端' : 'Only main backend built';
    }
    if (item.key === 'pid_impl' && activeBackends === 'native') {
      return zh ? '当前模块仅编入原生后端 已禁用非原生模式' : 'Only native backend built main disabled';
    }
    if (item.key === 'nsproxy' && activeBackends === 'main') {
      return zh ? '未编入原生后端 不可用' : 'Unavailable without native backend';
    }
    if (item.key === 'nsproxy' && formMain.pid_impl !== 'native') {
      return zh ? '需先将 PID 后端切为原生' : 'Requires native PID backend';
    }
    return item.desc[lang];
  };

  const renderGroup = (
    list: ParamSpec[],
    store: Record<string, string>,
    defaults: Record<string, string>,
    onChange: (k: string, v: string) => void
  ) =>
    list.map((item) => {
      const defVal = defaults[item.key] ?? item.def;
      const val = store[item.key] ?? defVal;
      const isGreen = item.type === 'bool' ? val === '1' : val !== defVal;
      const disabled = isParamDisabled(item);

      return (
        <div className="row" key={item.key}>
          <div className="row-info">
            <div className="row-label">
              <span>{item.label[lang]}</span>
              <span className={`chip ${isGreen ? 'chip-ok' : 'chip-muted'}`}>
                {`${dict.currentValPrefix}${val}`}
              </span>
            </div>
            <div className="row-desc">{paramDesc(item)}</div>
          </div>

          {item.type === 'bool' && (
            <MduiSwitch
              checked={val === '1'}
              disabled={disabled}
              onChange={(checked) => onChange(item.key, checked ? '1' : '0')}
            />
          )}

          {item.type === 'enum' && item.opts && (
            <MduiSegmentedGroup value={val} onChange={(next) => onChange(item.key, next)}>
              {item.opts[lang].map(([optVal, optLabel]) => (
                <mdui-segmented-button
                  key={optVal}
                  value={optVal}
                  disabled={isOptionDisabled(item, optVal)}
                >
                  {optLabel}
                </mdui-segmented-button>
              ))}
            </MduiSegmentedGroup>
          )}

          {(item.type === 'str' || item.type === 'num') && (
            <input
              type="text"
              className={`text-input ${item.type === 'num' ? 'num' : ''}`}
              value={val}
              onChange={(e) => onChange(item.key, e.target.value)}
            />
          )}
        </div>
      );
    });

  return (
    <>
      <div className="card">
        <div className="card-title">
          <span>{dict.payloadTitle}</span>
          <span className="chip chip-muted">{availText}</span>
        </div>
        {multiKmi && (
          <div className="row" style={{ flexWrap: 'wrap', gap: '10px' }}>
            <div className="row-info">
              <div className="row-label">
                <span>{dict.kmiSelectLabel}</span>
                {selectedKmi && <span className="chip chip-ok">{selectedKmi}</span>}
              </div>
              <div className="row-desc">{dict.kmiSelectDesc}</div>
            </div>
            {snapshot.kmis.length <= 3 ? (
              <MduiSegmentedGroup value={selectedKmi} onChange={onSelectKmi}>
                {snapshot.kmis.map((k) => (
                  <mdui-segmented-button key={k} value={k}>
                    {k}
                  </mdui-segmented-button>
                ))}
              </MduiSegmentedGroup>
            ) : (
              <select
                className="select-pill"
                value={selectedKmi}
                onChange={(e) => onSelectKmi(e.target.value)}
              >
                {snapshot.kmis.map((k) => (
                  <option key={k} value={k}>
                    {k}
                  </option>
                ))}
              </select>
            )}
          </div>
        )}
        <div className="row">
          <div className="row-info">
            <div className="row-label">
              <span>{dict.mainModLabel}</span>
              {activeBackends === 'native' && <span className="chip chip-ok">native</span>}
              {activeBackends === 'both' && <span className="chip chip-info">main&amp;native</span>}
            </div>
            <div className="row-desc">{dict.mainModDesc}</div>
          </div>
          <MduiSwitch checked={modMainEnabled} onChange={onToggleModMain} />
        </div>
        <div className="row">
          <div className="row-info">
            <div className="row-label">{dict.vendorModLabel}</div>
            <div className="row-desc">{dict.vendorModDesc}</div>
          </div>
          <MduiSwitch
            checked={snapshot.hasVendorKo && modVendorEnabled}
            disabled={!snapshot.hasVendorKo}
            onChange={onToggleModVendor}
          />
        </div>
      </div>

      <div className="card">
        <div className="card-title">
          <span>{dict.commonParamsTitle}</span>
        </div>
        {renderGroup(PARAMS_SCHEMA.common, formMain, effectiveMainDefs, onChangeMain)}
      </div>

      {snapshot.hasVendorKo && (
        <div className="card">
          <div className="card-title">
            <span>{dict.vendorParamsTitle}</span>
          </div>
          {renderGroup(PARAMS_SCHEMA.vendor, formVendor, {}, onChangeVendor)}
        </div>
      )}

      <details className="fold">
        <summary>
          <span>{dict.diagParamsTitle}</span>
          <span className="chip chip-warn">{dict.advancedTag}</span>
        </summary>
        <div className="fold-body">
          {renderGroup(PARAMS_SCHEMA.diag, formMain, effectiveMainDefs, onChangeMain)}
        </div>
      </details>

      <div className="card">
        <div className="card-title">
          <span>{dict.previewTitle}</span>
          <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
            {unsaved && <span className="chip chip-warn">{dict.unsavedTag}</span>}
            <mdui-button variant="tonal" onClick={onAddExtra}>
              {dict.addExtraBtn}
            </mdui-button>
          </div>
        </div>

        {extraParams.length > 0 && (
          <div className="extra-list">
            {extraParams.map((item) => (
              <div className="extra-row" key={item.id}>
                <select
                  className="select-pill"
                  value={item.mod}
                  onChange={(e) =>
                    onUpdateExtra(item.id, 'mod', e.target.value as 'droid_lkm' | 'droid_lkm_vendor')
                  }
                >
                  <option value="droid_lkm">droid_lkm</option>
                  {snapshot.hasVendorKo && <option value="droid_lkm_vendor">droid_lkm_vendor</option>}
                </select>
                <input
                  type="text"
                  className="text-input"
                  value={item.key}
                  placeholder={dict.extraKeyPh}
                  onChange={(e) => onUpdateExtra(item.id, 'key', e.target.value)}
                />
                <input
                  type="text"
                  className="text-input"
                  value={item.val}
                  placeholder={dict.extraValPh}
                  onChange={(e) => onUpdateExtra(item.id, 'val', e.target.value)}
                />
                <button
                  type="button"
                  className="icon-btn"
                  aria-label="remove"
                  onClick={() => onRemoveExtra(item.id)}
                >
                  <CloseIcon size={16} />
                </button>
              </div>
            ))}
          </div>
        )}

        <div className="preview-box">{previewText}</div>

        <div className="action-bar">
          <mdui-button variant="outlined" disabled={busy} onClick={onResetDefaults}>
            {dict.resetBtn}
          </mdui-button>
          <mdui-button
            variant="tonal"
            disabled={snapshot.loaded || snapshot.meta.blocked || busy}
            onClick={onInstallOnce}
          >
            {dict.runTempBtn}
          </mdui-button>
          <mdui-button variant="filled" disabled={busy} onClick={onSaveConfig}>
            {dict.saveBtn}
          </mdui-button>
        </div>
      </div>
    </>
  );
}
