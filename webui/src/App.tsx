// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

import { setColorScheme, setTheme, snackbar } from 'mdui';
import { useCallback, useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import {
  clearBootBlock,
  clearModuleLogs,
  fetchLogs,
  fetchStatus,
  getMockFlavor,
  getMockScenario,
  installModules,
  isNativeHost,
  saveConfigText,
  setAutoBoot,
  setMockFlavor,
  setMockScenario
} from './api/bridge';
import { parseSavedConfig } from './api/parser';
import { LogsIcon, OverviewIcon, PaletteIcon, ParamsIcon, RefreshIcon, ThemeIcon } from './components/Icons';
import { LogsPage } from './components/LogsPage';
import { OverviewPage } from './components/OverviewPage';
import { PaletteDialog } from './components/PaletteDialog';
import { ParamsPage } from './components/ParamsPage';
import { DEFAULT_SEED_COLOR, getLangStrings, majorMinor, PARAMS_SCHEMA } from './i18n';
import type {
  ExtraParam,
  Lang,
  LogKind,
  ParamSpec,
  ResolvedFlavor,
  ScenarioKey,
  StatusSnapshot,
  TabKey,
  ThemeMode
} from './types';
import {
  effectiveMainDefaults,
  getFlavorCapabilities,
  harmonizeMainParams,
  isBuildLockedFlavor,
  resolveEffectiveFlavor,
  serializeWithEffectiveDefaults
} from './variants/flavor';

function readStored(key: string): string | null {
  try {
    return window.localStorage.getItem(key);
  } catch {
    return null;
  }
}

function writeStored(key: string, val: string) {
  try {
    window.localStorage.setItem(key, val);
  } catch {
    // ignore storage failures in restricted webviews
  }
}

function defaultStore(list: ParamSpec[]): Record<string, string> {
  const out: Record<string, string> = {};
  list.forEach((item) => {
    out[item.key] = item.def;
  });
  return out;
}

function serializeGroup(store: Record<string, string>, schema: typeof PARAMS_SCHEMA.common): string {
  const out: string[] = [];
  schema.forEach((item) => {
    const val = store[item.key] ?? item.def;
    if (val !== item.def && val !== '') {
      out.push(`${item.key}=${val}`);
    }
  });
  return out.join(' ');
}

function upsertKmiLine(configText: string, kmi: string): string {
  const rest = String(configText || '')
    .split(/\r?\n/)
    .filter((l) => l.trim() !== '' && !/^kmi=/.test(l.trim()));
  return [`kmi=${kmi}`, ...rest].join('\n');
}

function showToast(message: string) {
  snackbar({ message, placement: 'bottom' });
}

export function App() {
  const native = isNativeHost();
  const [lang, setLang] = useState<Lang>(() => (readStored('phdroid_lang') === 'en' ? 'en' : 'zh'));
  const [theme, setThemeState] = useState<ThemeMode>(() => {
    const stored = readStored('phdroid_theme');
    return stored === 'dark' || stored === 'light' ? stored : 'auto';
  });
  const [seedColor, setSeedColor] = useState<string>(
    () => readStored('phdroid_seed_color') || DEFAULT_SEED_COLOR
  );
  const [paletteOpen, setPaletteOpen] = useState(false);

  const [tab, setTab] = useState<TabKey>('overview');
  const [logTab, setLogTab] = useState<LogKind>('module');
  const [scenario, setScenario] = useState<ScenarioKey>(() => getMockScenario());
  const [previewFlavor, setPreviewFlavor] = useState<ResolvedFlavor>(() => getMockFlavor());
  const flavorLocked = isBuildLockedFlavor();

  const [snapshot, setSnapshot] = useState<StatusSnapshot | null>(null);
  const [busy, setBusy] = useState(false);
  const [installPulse, setInstallPulse] = useState(0);

  const [selectedKmi, setSelectedKmi] = useState('');
  const [modMainEnabled, setModMainEnabled] = useState(true);
  const [modVendorEnabled, setModVendorEnabled] = useState(true);
  const [formMain, setFormMain] = useState<Record<string, string>>(() =>
    defaultStore(PARAMS_SCHEMA.common.concat(PARAMS_SCHEMA.diag))
  );
  const [formVendor, setFormVendor] = useState<Record<string, string>>(() =>
    defaultStore(PARAMS_SCHEMA.vendor)
  );
  const [extraParams, setExtraParams] = useState<ExtraParam[]>([]);
  const extraIdRef = useRef(1);

  const [moduleLogs, setModuleLogs] = useState<string[]>([]);
  const [dmesgLogs, setDmesgLogs] = useState<string[]>([]);
  const [logSearch, setLogSearch] = useState('');
  const [onlyPhdroid, setOnlyPhdroid] = useState(true);
  const [onlyWarnErr, setOnlyWarnErr] = useState(false);

  const dict = getLangStrings(lang);

  useEffect(() => {
    setTheme(theme);
    writeStored('phdroid_theme', theme);
  }, [theme]);

  useEffect(() => {
    setColorScheme(seedColor);
    writeStored('phdroid_seed_color', seedColor);
  }, [seedColor]);

  useEffect(() => {
    writeStored('phdroid_lang', lang);
  }, [lang]);

  const effectiveFlavor = useMemo(
    () => resolveEffectiveFlavor(previewFlavor, native, snapshot),
    [previewFlavor, native, snapshot]
  );
  const flavorCaps = useMemo(
    () => getFlavorCapabilities(effectiveFlavor, snapshot),
    [effectiveFlavor, snapshot]
  );
  const activeBackends = flavorCaps.backends;
  const effectiveMainDefs = useMemo(
    () => effectiveMainDefaults(activeBackends),
    [activeBackends]
  );

  const applyConfigToForm = useCallback(
    (snap: StatusSnapshot, flavorOverride?: ResolvedFlavor) => {
      const resolvedFl =
        flavorOverride ?? resolveEffectiveFlavor(previewFlavor, native, snap);
      const backends = getFlavorCapabilities(resolvedFl, snap).backends;
      const parsed = parseSavedConfig(snap.savedConfigText);

      const mainDefs = effectiveMainDefaults(backends);
      const vendorDefs = defaultStore(PARAMS_SCHEMA.vendor);
      const mainKnown = new Set(Object.keys(mainDefs));
      const vendorKnown = new Set(Object.keys(vendorDefs));
      const nextExtras: ExtraParam[] = [];

      const parseTokens = (
        mod: 'droid_lkm' | 'droid_lkm_vendor',
        line: string,
        known: Set<string>,
        target: Record<string, string>
      ) => {
        line
          .split(/\s+/)
          .filter(Boolean)
          .forEach((tok) => {
            const eq = tok.indexOf('=');
            if (eq <= 0) return;
            const k = tok.slice(0, eq);
            const v = tok.slice(eq + 1);
            if (known.has(k)) {
              target[k] = v;
            } else {
              nextExtras.push({ id: extraIdRef.current++, mod, key: k, val: v });
            }
          });
      };

      parseTokens('droid_lkm', parsed.droid_lkm, mainKnown, mainDefs);
      parseTokens('droid_lkm_vendor', parsed.droid_lkm_vendor, vendorKnown, vendorDefs);

      const hasMultiKmi = snap.kmis.length > 1;
      const nextKmi = hasMultiKmi
        ? snap.kmis.includes(parsed.kmi)
          ? parsed.kmi
          : snap.kmis[0]
        : '';

      const hasMods = parsed.modules.length > 0;
      setSelectedKmi(nextKmi);
      setModMainEnabled(hasMods ? parsed.modules.includes('droid_lkm') : true);
      setModVendorEnabled(
        snap.hasVendorKo && (hasMods ? parsed.modules.includes('droid_lkm_vendor') : true)
      );
      setFormMain(harmonizeMainParams(mainDefs, backends));
      setFormVendor(vendorDefs);
      setExtraParams(nextExtras);
    },
    [native, previewFlavor]
  );

  const loadAll = useCallback(
    async (syncForm: boolean, toast?: string, flavorOverride?: ResolvedFlavor) => {
      setBusy(true);
      try {
        const [snap, modLogs, kLogs] = await Promise.all([
          fetchStatus(),
          fetchLogs('module'),
          fetchLogs('dmesg')
        ]);
        setSnapshot(snap);
        setModuleLogs(modLogs);
        setDmesgLogs(kLogs);
        if (syncForm) {
          applyConfigToForm(snap, flavorOverride);
        }
        if (toast) showToast(toast);
      } catch (err) {
        showToast(err instanceof Error ? err.message : String(err));
      } finally {
        setBusy(false);
      }
    },
    [applyConfigToForm]
  );

  useEffect(() => {
    void loadAll(true);
  }, [loadAll]);

  const previewText = useMemo(() => {
    const mods: string[] = [];
    if (modMainEnabled) mods.push('droid_lkm');
    if (snapshot?.hasVendorKo && modVendorEnabled) mods.push('droid_lkm_vendor');

    const extraFor = (mod: 'droid_lkm' | 'droid_lkm_vendor') =>
      extraParams
        .filter((p) => p.mod === mod)
        .map((p) => ({ k: p.key.trim().replace(/\s+/g, ''), v: p.val.trim() }))
        .filter((p) => p.k !== '' && p.v !== '')
        .map((p) => `${p.k}=${p.v}`)
        .join(' ');

    const mainArgs = serializeWithEffectiveDefaults(
      formMain,
      PARAMS_SCHEMA.common.concat(PARAMS_SCHEMA.diag),
      effectiveMainDefs
    );
    const fullMain = [mainArgs, extraFor('droid_lkm')].filter(Boolean).join(' ');
    const vendorArgs = serializeGroup(formVendor, PARAMS_SCHEMA.vendor);
    const fullVendor = [vendorArgs, extraFor('droid_lkm_vendor')].filter(Boolean).join(' ');

    const lines: string[] = [];
    if (snapshot && snapshot.kmis.length > 1 && selectedKmi) {
      lines.push(`kmi=${selectedKmi}`);
    }
    lines.push(`modules=${mods.join(' ')}`);
    if (fullMain) lines.push(`droid_lkm=${fullMain}`);
    if (fullVendor) lines.push(`droid_lkm_vendor=${fullVendor}`);
    return lines.join('\n');
  }, [
    snapshot,
    selectedKmi,
    modMainEnabled,
    modVendorEnabled,
    formMain,
    formVendor,
    extraParams,
    effectiveMainDefs
  ]);

  const unsaved = snapshot ? previewText !== snapshot.savedConfigText : false;
  const savedKmi = useMemo(
    () => (snapshot ? parseSavedConfig(snapshot.savedConfigText).kmi : ''),
    [snapshot]
  );
  const kmiConfirmed = useMemo(() => {
    if (!snapshot || snapshot.kmis.length <= 1) return true;
    if (snapshot.loaded) return true;
    return snapshot.kmis.includes(savedKmi) && selectedKmi === savedKmi;
  }, [snapshot, savedKmi, selectedKmi]);

  const handleToggleTheme = () => {
    const sysDark = Boolean(window.matchMedia?.('(prefers-color-scheme: dark)').matches);
    const currentlyDark = theme === 'dark' || (theme === 'auto' && sysDark);
    const next: ThemeMode = currentlyDark ? 'light' : 'dark';
    setThemeState(next);
    showToast(next === 'dark' ? dict.toasts.themeDark : dict.toasts.themeLight);
  };

  const handleScenarioChange = (next: ScenarioKey) => {
    setMockScenario(next);
    setScenario(next);
    setInstallPulse(0);
    void loadAll(true);
  };

  const handleFlavorChange = (next: ResolvedFlavor) => {
    setMockFlavor(next);
    setPreviewFlavor(next);
    setInstallPulse(0);
    void loadAll(true, undefined, next);
  };

  const handleToggleAuto = async (enable: boolean) => {
    if (enable && !kmiConfirmed) {
      showToast(dict.toasts.kmiNeedConfirm);
      return;
    }
    setBusy(true);
    try {
      await setAutoBoot(enable);
      await loadAll(false, enable ? dict.toasts.autoOn : dict.toasts.autoOff);
    } catch (err) {
      showToast(err instanceof Error ? err.message : String(err));
      setBusy(false);
    }
  };

  const handleUnblock = async () => {
    setBusy(true);
    try {
      await clearBootBlock();
      await loadAll(false, dict.toasts.unblocked);
    } catch (err) {
      showToast(err instanceof Error ? err.message : String(err));
      setBusy(false);
    }
  };

  const handleInstall = async (useForm: boolean) => {
    if (snapshot?.meta.blocked) {
      showToast(dict.toasts.blockedGuard);
      return;
    }
    if (!kmiConfirmed && !useForm) {
      showToast(dict.toasts.kmiNeedConfirm);
      return;
    }
    if (snapshot?.loaded) {
      showToast(dict.toasts.alreadyLoaded);
      return;
    }
    setBusy(true);
    try {
      await installModules(useForm ? previewText : undefined);
      setTab('overview');
      await loadAll(false, dict.toasts.installed);
      setInstallPulse((n) => n + 1);
    } catch (err) {
      showToast(`${dict.toasts.installFail}: ${err instanceof Error ? err.message : String(err)}`);
      setBusy(false);
    }
  };

  const handleConfirmKmi = async (kmiToSave: string) => {
    if (!snapshot || !kmiToSave) return;
    setSelectedKmi(kmiToSave);
    setBusy(true);
    try {
      const nextText = upsertKmiLine(snapshot.savedConfigText, kmiToSave);
      await saveConfigText(nextText);
      await loadAll(false, `${dict.toasts.kmiConfirmed}: ${kmiToSave}`);
    } catch (err) {
      showToast(`${dict.toasts.saveFail}: ${err instanceof Error ? err.message : String(err)}`);
      setBusy(false);
    }
  };

  const handleSaveConfig = async () => {
    setBusy(true);
    try {
      await saveConfigText(previewText);
      await loadAll(false, dict.toasts.saved);
    } catch (err) {
      showToast(`${dict.toasts.saveFail}: ${err instanceof Error ? err.message : String(err)}`);
      setBusy(false);
    }
  };

  const handleChangeMain = (key: string, val: string) => {
    setFormMain((prev) => {
      const next = { ...prev, [key]: val };
      if (key === 'pid_impl' && val !== 'native') {
        next.nsproxy = '0';
      }
      return next;
    });
  };

  const handleResetDefaults = () => {
    setSelectedKmi(snapshot && snapshot.kmis.length > 1 ? snapshot.kmis[0] : '');
    setModMainEnabled(true);
    setModVendorEnabled(Boolean(snapshot?.hasVendorKo));
    setFormMain(effectiveMainDefaults(activeBackends));
    setFormVendor(defaultStore(PARAMS_SCHEMA.vendor));
    setExtraParams([]);
    showToast(dict.toasts.reset);
  };

  const handleClearModuleLogs = async () => {
    setBusy(true);
    try {
      await clearModuleLogs();
      setModuleLogs([]);
      showToast(dict.toasts.cleared);
    } catch (err) {
      showToast(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy(false);
    }
  };

  const handleCopyLogs = async (text: string) => {
    try {
      if (navigator.clipboard?.writeText) {
        await navigator.clipboard.writeText(text);
      }
      showToast(dict.toasts.copied);
    } catch {
      showToast(dict.toasts.copied);
    }
  };

  const navItems: { key: TabKey; label: string; icon: ReactNode }[] = [
    { key: 'overview', label: dict.navOverview, icon: <OverviewIcon /> },
    { key: 'config', label: dict.navConfig, icon: <ParamsIcon /> },
    { key: 'logs', label: dict.navLogs, icon: <LogsIcon /> }
  ];

  return (
    <div className="app-shell">
      <aside className="nav-rail">
        {navItems.map((item) => (
          <button
            type="button"
            key={item.key}
            className={`nav-item ${tab === item.key ? 'active' : ''}`}
            onClick={() => setTab(item.key)}
          >
            <span className="nav-pill">{item.icon}</span>
            <span>{item.label}</span>
          </button>
        ))}
      </aside>

      <div className="main-wrap">
        <header className="top-bar">
          <div className="top-bar-title">
            <h1>phDroid</h1>
            <span className="top-bar-sub">
              {snapshot ? majorMinor(snapshot.meta.release) : '...'}
            </span>
          </div>
          <div className="top-actions">
            {!native && !flavorLocked && (
              <select
                className="select-pill"
                value={previewFlavor}
                onChange={(e) => handleFlavorChange(e.target.value as ResolvedFlavor)}
              >
                {(['standard', 'native', 'main-native'] as ResolvedFlavor[]).map((fl) => (
                  <option key={fl} value={fl}>
                    {dict.flavorNames[fl]}
                  </option>
                ))}
              </select>
            )}
            {!native && (
              <select
                className="select-pill"
                value={scenario}
                onChange={(e) => handleScenarioChange(e.target.value as ScenarioKey)}
              >
                {(Object.keys(dict.scenarios) as ScenarioKey[]).map((key) => (
                  <option key={key} value={key}>
                    {dict.scenarios[key]}
                  </option>
                ))}
              </select>
            )}
            <button
              type="button"
              className="lang-btn"
              onClick={() => setLang((prev) => (prev === 'zh' ? 'en' : 'zh'))}
            >
              {lang === 'zh' ? 'EN' : '中'}
            </button>
            <button
              type="button"
              className="icon-btn primary"
              title={dict.paletteTitle}
              aria-label={dict.paletteTitle}
              onClick={() => setPaletteOpen(true)}
            >
              <PaletteIcon />
            </button>
            <button
              type="button"
              className="icon-btn"
              aria-label="theme"
              onClick={handleToggleTheme}
            >
              <ThemeIcon />
            </button>
            <button
              type="button"
              className="icon-btn"
              aria-label="refresh"
              disabled={busy}
              onClick={() => void loadAll(false, dict.toasts.refreshed)}
            >
              <RefreshIcon />
            </button>
          </div>
        </header>

        <main className="content">
          {snapshot && tab === 'overview' && (
            <OverviewPage
              lang={lang}
              snapshot={snapshot}
              busy={busy}
              installPulse={installPulse}
              selectedKmi={selectedKmi}
              savedKmi={savedKmi}
              kmiConfirmed={kmiConfirmed}
              onSelectKmi={setSelectedKmi}
              onConfirmKmi={(k) => void handleConfirmKmi(k)}
              onToggleAuto={(v) => void handleToggleAuto(v)}
              onUnblock={() => void handleUnblock()}
              onInstallNow={() => void handleInstall(false)}
              onShortTapInstall={() => showToast(dict.holdToConfirmHint)}
              onGoConfig={() => setTab('config')}
            />
          )}

          {snapshot && tab === 'config' && (
            <ParamsPage
              lang={lang}
              snapshot={snapshot}
              busy={busy}
              selectedKmi={selectedKmi}
              activeBackends={activeBackends}
              effectiveMainDefs={effectiveMainDefs}
              modMainEnabled={modMainEnabled}
              modVendorEnabled={modVendorEnabled}
              formMain={formMain}
              formVendor={formVendor}
              extraParams={extraParams}
              previewText={previewText}
              unsaved={unsaved}
              onSelectKmi={setSelectedKmi}
              onToggleModMain={setModMainEnabled}
              onToggleModVendor={setModVendorEnabled}
              onChangeMain={handleChangeMain}
              onChangeVendor={(k, v) => setFormVendor((prev) => ({ ...prev, [k]: v }))}
              onAddExtra={() =>
                setExtraParams((prev) => [
                  ...prev,
                  { id: extraIdRef.current++, mod: 'droid_lkm', key: '', val: '' }
                ])
              }
              onRemoveExtra={(id) =>
                setExtraParams((prev) => prev.filter((item) => item.id !== id))
              }
              onUpdateExtra={(id, field, value) =>
                setExtraParams((prev) =>
                  prev.map((item) => (item.id === id ? { ...item, [field]: value } : item))
                )
              }
              onResetDefaults={handleResetDefaults}
              onInstallOnce={() => void handleInstall(true)}
              onSaveConfig={() => void handleSaveConfig()}
            />
          )}

          {snapshot && tab === 'logs' && (
            <LogsPage
              lang={lang}
              logTab={logTab}
              moduleLogs={moduleLogs}
              dmesgLogs={dmesgLogs}
              searchQuery={logSearch}
              onlyPhdroid={onlyPhdroid}
              onlyWarnErr={onlyWarnErr}
              busy={busy}
              onSwitchLogTab={setLogTab}
              onSearchChange={setLogSearch}
              onToggleOnlyPhdroid={setOnlyPhdroid}
              onToggleOnlyWarnErr={setOnlyWarnErr}
              onCopyLogs={(txt) => void handleCopyLogs(txt)}
              onClearModuleLogs={() => void handleClearModuleLogs()}
            />
          )}
        </main>
      </div>

      <nav className="nav-bar">
        {navItems.map((item) => (
          <button
            type="button"
            key={item.key}
            className={`nav-item ${tab === item.key ? 'active' : ''}`}
            onClick={() => setTab(item.key)}
          >
            <span className="nav-pill">{item.icon}</span>
            <span>{item.label}</span>
          </button>
        ))}
      </nav>

      <PaletteDialog
        open={paletteOpen}
        lang={lang}
        seedColor={seedColor}
        onSelectColor={setSeedColor}
        onClose={() => setPaletteOpen(false)}
      />
    </div>
  );
}
