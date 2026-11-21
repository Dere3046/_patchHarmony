// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

import type {
  FeaturesData,
  HookEntry,
  LayoutEntry,
  PidBackendMask,
  RuntimeInfo,
  SavedConfigMap,
  StatusSnapshot
} from '../types';

export function parseKeyValues(text: string): Record<string, string> {
  const out: Record<string, string> = {};
  String(text || '')
    .split(/\r?\n/)
    .forEach((line) => {
      const idx = line.indexOf('=');
      if (idx > 0) {
        out[line.slice(0, idx).trim()] = line.slice(idx + 1).trim();
      }
    });
  return out;
}

export function normalizeSysParamValue(val: string): string {
  const v = val.trim();
  if (v === 'Y' || v === 'y') return '1';
  if (v === 'N' || v === 'n') return '0';
  return v;
}

export function parseSysParams(text: string): Record<string, string> {
  const raw = parseKeyValues(text);
  const out: Record<string, string> = {};
  Object.keys(raw).forEach((k) => {
    out[k] = normalizeSysParamValue(raw[k]);
  });
  return out;
}

export function parseSavedConfig(text: string): SavedConfigMap {
  const raw = parseKeyValues(text);
  return {
    kmi: raw.kmi || '',
    modules: (raw.modules || '').split(/\s+/).filter(Boolean),
    droid_lkm: raw.droid_lkm || '',
    droid_lkm_vendor: raw.droid_lkm_vendor || '',
    raw
  };
}

function emptyRuntime(): RuntimeInfo {
  return {
    moduleReady: 'no',
    criticalHooks: 'degraded',
    degrade: 'no',
    gate: 'global',
    hookRoutePolicy: 'auto',
    pidns: 'off',
    pidnsImpl: 'main',
    pidnsBackends: 'main',
    nsproxy: '0',
    ipcns: 'off',
    mqueueFs: 'off',
    mqueueShim: 'no',
    procNs: 'off',
    nspidStatus: 'off',
    nspidInsert: 'off',
    syscallSlots: 'off',
    slotsClone: 'no',
    slotsIpc64: '0/0',
    slotsIpc32: '0/0'
  };
}

export function parseFeaturesText(text: string): FeaturesData | null {
  const raw = String(text || '').trim();
  if (!raw) return null;

  const capabilities: FeaturesData['capabilities'] = {
    ipc_ns: { owner: 'absent', reason: '' },
    posix_mqueue: { owner: 'absent', reason: '' },
    sysvipc: { owner: 'absent', reason: '' },
    compat32: { owner: 'absent', reason: '' }
  };
  let layoutTrusted = false;
  const layout: LayoutEntry[] = [];
  const hooks: HookEntry[] = [];
  const runtime = emptyRuntime();

  type Section = 'none' | 'feature' | 'layout' | 'hook' | 'runtime';
  let section: Section = 'none';

  raw.split(/\r?\n/).forEach((line) => {
    const trimmed = line.trim();
    if (!trimmed) return;

    if (/^feature\s+owner/i.test(trimmed)) {
      section = 'feature';
      return;
    }
    const ltMatch = trimmed.match(/^layout trusted:\s*(yes|no)$/i);
    if (ltMatch) {
      layoutTrusted = ltMatch[1].toLowerCase() === 'yes';
      section = 'layout';
      return;
    }
    if (/^hook\s+route\s+state/i.test(trimmed)) {
      section = 'hook';
      return;
    }
    if (/^runtime\s+state/i.test(trimmed)) {
      section = 'runtime';
      return;
    }

    if (section === 'feature') {
      const m = trimmed.match(/^(\S+)\s+(\S+)(?:\s+(.*))?$/);
      if (m) {
        const key = m[1] as keyof FeaturesData['capabilities'];
        if (key in capabilities) {
          capabilities[key] = { owner: m[2], reason: (m[3] || '').trim() };
        }
      }
      return;
    }

    if (section === 'layout') {
      // Handles both "task_struct.nsproxy ok 1920/1920" and "sizeof(pid_namespace   ) ok 176/176"
      const m = trimmed.match(/^(.+?)\s+(\S+)\s+(\d+)\/(\d+)$/);
      if (m) {
        const cleanField = m[1].replace(/\s+\)$/, ')').trim();
        layout.push({
          field: cleanField,
          state: m[2],
          live: m[3],
          build: m[4]
        });
      }
      return;
    }

    if (section === 'hook') {
      const m = trimmed.match(/^(\S+)\s+(\S+)\s+(\S+)$/);
      if (m) {
        hooks.push({ name: m[1], route: m[2], state: m[3] });
      }
      return;
    }

    if (section === 'runtime') {
      const m = trimmed.match(/^(\S+)\s+(\S+)(?:\s+(.*))?$/);
      if (!m) return;
      const [, key, state, detail = ''] = m;
      switch (key) {
        case 'module_ready':
          runtime.moduleReady = state;
          break;
        case 'critical_hooks':
          runtime.criticalHooks = state;
          break;
        case 'degrade':
          runtime.degrade = state;
          break;
        case 'gate':
          runtime.gate = state;
          break;
        case 'hook_route_policy':
          runtime.hookRoutePolicy = state;
          break;
        case 'pidns': {
          runtime.pidns = state;
          const implM = detail.match(/impl=(\S+)/);
          const backM = detail.match(/backends=(\S+)/);
          const nspM = detail.match(/nsproxy=(\S+)/);
          if (implM) runtime.pidnsImpl = implM[1];
          if (backM) runtime.pidnsBackends = backM[1];
          if (nspM) runtime.nsproxy = nspM[1];
          break;
        }
        case 'ipcns':
          runtime.ipcns = state;
          break;
        case 'mqueue_fs': {
          runtime.mqueueFs = state;
          const shimM = detail.match(/shim=(\S+)/);
          if (shimM) runtime.mqueueShim = shimM[1];
          break;
        }
        case 'proc_ns':
          runtime.procNs = state;
          break;
        case 'nspid_status': {
          runtime.nspidStatus = state;
          const insM = detail.match(/insert=(\S+)/);
          if (insM) runtime.nspidInsert = insM[1];
          break;
        }
        case 'syscall_slots': {
          runtime.syscallSlots = state;
          const clM = detail.match(/clone=(\S+)/);
          const i64M = detail.match(/ipc64=(\S+)/);
          const i32M = detail.match(/ipc32=(\S+)/);
          if (clM) runtime.slotsClone = clM[1];
          if (i64M) runtime.slotsIpc64 = i64M[1];
          if (i32M) runtime.slotsIpc32 = i32M[1];
          break;
        }
      }
    }
  });

  return { capabilities, layoutTrusted, layout, hooks, runtime };
}

export function parseStatusOutput(rawText: string): StatusSnapshot {
  const sections: Record<string, string[]> = {};
  let current = '';

  String(rawText || '')
    .split(/\r?\n/)
    .forEach((line) => {
      const m = line.match(/^###PHDROID:([^#]+)###$/);
      if (m) {
        current = m[1];
        if (!sections[current]) sections[current] = [];
      } else if (current) {
        sections[current].push(line);
      }
    });

  const metaKv = parseKeyValues((sections.meta || []).join('\n'));
  const kmis = (sections.kmis || []).map((s) => s.trim()).filter(Boolean);
  const kos = (sections.kos || []).map((s) => s.trim()).filter(Boolean);
  const modLines = (sections.modules || []).map((s) => s.trim()).filter(Boolean);
  const loaded = modLines.some((l) => /^droid_lkm\s/.test(l));
  const vendorLoaded = modLines.some((l) => /^droid_lkm_vendor\s/.test(l));
  const hasVendorKo = kos.includes('droid_lkm_vendor.ko');
  const defaultConfig = hasVendorKo ? 'modules=droid_lkm droid_lkm_vendor' : 'modules=droid_lkm';
  const rawConfig = (sections.config || []).join('\n').trim();
  const configText = /^modules=/m.test(rawConfig) ? rawConfig : defaultConfig;
  const pidBackends = (['main', 'native', 'both'].includes(metaKv.pid_backends || '')
    ? metaKv.pid_backends
    : 'unknown') as PidBackendMask;

  return {
    meta: {
      release: metaKv.release || '',
      kmi: metaKv.kmi || '',
      pidBackends,
      moddir: metaKv.moddir || '',
      data: metaKv.data || '/data/adb/phdroid',
      auto: metaKv.auto === '1',
      blocked: metaKv.blocked === '1',
      insmodMark: metaKv.insmod_mark === '1',
      mqueueFs: metaKv.mqueue_fs === '1'
    },
    kmis: kmis.length ? kmis : metaKv.kmi ? [metaKv.kmi] : [],
    kos,
    loaded,
    vendorLoaded,
    hasVendorKo,
    savedConfigText: configText,
    activeMainParams: parseSysParams((sections.params_droid_lkm || []).join('\n')),
    activeVendorParams: parseSysParams((sections.params_droid_lkm_vendor || []).join('\n')),
    features: parseFeaturesText((sections.features || []).join('\n'))
  };
}
