// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

import { PARAMS_SCHEMA } from '../i18n';
import type {
  FeaturesData,
  LogKind,
  PidBackendMask,
  ResolvedFlavor,
  ScenarioKey,
  StatusSnapshot
} from '../types';
import { getCompiledFlavor } from '../variants/flavor';
import { parseSavedConfig } from './parser';

function kvToObj(str: string, base: Record<string, string>): Record<string, string> {
  const out = { ...base };
  String(str || '')
    .split(/\s+/)
    .filter(Boolean)
    .forEach((pair) => {
      const idx = pair.indexOf('=');
      if (idx > 0) out[pair.slice(0, idx)] = pair.slice(idx + 1);
    });
  return out;
}

function makeFullFeatures(backends: PidBackendMask = 'main', impl: 'main' | 'native' = 'main'): FeaturesData {
  return {
    capabilities: {
      ipc_ns: { owner: 'module', reason: '' },
      posix_mqueue: { owner: 'module', reason: '' },
      sysvipc: { owner: 'module', reason: '' },
      compat32: { owner: 'kernel', reason: '' }
    },
    layoutTrusted: true,
    layout: [
      { field: 'task_struct.nsproxy', state: 'ok', live: '1920', build: '1920' },
      { field: 'task_struct.thread_pid', state: 'ok', live: '1560', build: '1560' },
      { field: 'pid_namespace.level', state: 'ok', live: '72', build: '72' },
      { field: 'pid_namespace.idr', state: 'ok', live: '8', build: '8' },
      { field: 'ipc_namespace.ids', state: 'ok', live: '64', build: '64' },
      { field: 'sizeof(pid_namespace)', state: 'ok', live: '176', build: '176' }
    ],
    hooks: [
      { name: 'create_new_namespaces', route: 'text', state: 'installed' },
      { name: 'copy_namespaces', route: 'text', state: 'installed' },
      { name: 'free_nsproxy', route: 'text', state: 'installed' },
      { name: 'proc_ns_dir_lookup', route: 'slot', state: 'installed' },
      { name: 'proc_pid_status', route: 'text', state: 'installed' }
    ],
    runtime: {
      moduleReady: 'yes',
      criticalHooks: 'ok',
      degrade: 'no',
      gate: 'global',
      hookRoutePolicy: 'auto',
      pidns: 'ready',
      pidnsImpl: impl,
      pidnsBackends: backends,
      nsproxy: impl === 'native' ? '1' : '0',
      ipcns: 'ready',
      mqueueFs: 'up',
      mqueueShim: 'yes',
      procNs: 'ready',
      nspidStatus: 'ready',
      nspidInsert: 'inline',
      syscallSlots: 'ready',
      slotsClone: 'yes',
      slotsIpc64: '18/18',
      slotsIpc32: '18/18'
    }
  };
}

function flavorDefaultBackends(flavor: ResolvedFlavor): PidBackendMask {
  if (flavor === 'native') return 'native';
  if (flavor === 'main-native') return 'both';
  return 'main';
}

function makeScenarioSnapshot(key: ScenarioKey, flavor: ResolvedFlavor): StatusSnapshot {
  const baseBackends = flavorDefaultBackends(flavor);
  const baseImpl: 'main' | 'native' = baseBackends === 'native' ? 'native' : 'main';
  const fullFeatures = makeFullFeatures(baseBackends, baseImpl);

  if (key === 'full') {
    return {
      meta: {
        release: '6.12.38-android16-5-g8c67d4274c0a-ab14275539-4k',
        kmi: 'android16-6.12',
        pidBackends: baseBackends,
        moddir: '/data/adb/modules/phdroid',
        data: '/data/adb/phdroid',
        auto: true,
        blocked: false,
        insmodMark: false,
        mqueueFs: true
      },
      kmis: ['android16-6.12'],
      kos: ['droid_lkm.ko', 'droid_lkm_vendor.ko'],
      loaded: true,
      vendorLoaded: true,
      hasVendorKo: true,
      savedConfigText: 'modules=droid_lkm droid_lkm_vendor',
      activeMainParams: { route: '0', gate: '0', mqueue: '1', nspid: '1', pid_impl: baseImpl },
      activeVendorParams: { ghost: '1', ghost_match: 'oplus_' },
      features: fullFeatures
    };
  }
  if (key === 'degraded') {
    return {
      meta: {
        release: '6.12.38-android16-5-g8c67d4274c0a-ab14275539-4k',
        kmi: 'android16-6.12',
        pidBackends: baseBackends,
        moddir: '/data/adb/modules/phdroid',
        data: '/data/adb/phdroid',
        auto: true,
        blocked: false,
        insmodMark: false,
        mqueueFs: false
      },
      kmis: ['android16-6.12'],
      kos: ['droid_lkm.ko', 'droid_lkm_vendor.ko'],
      loaded: true,
      vendorLoaded: true,
      hasVendorKo: true,
      savedConfigText:
        'modules=droid_lkm droid_lkm_vendor\ndroid_lkm=route=2 gate=1 nspid_append=1 degrade=1 verbose=1',
      activeMainParams: {
        route: '2',
        gate: '1',
        nspid_append: '1',
        degrade: '1',
        verbose: '1',
        pid_impl: baseImpl
      },
      activeVendorParams: { ghost: '1', ghost_match: 'oplus_' },
      features: {
        capabilities: {
          ipc_ns: { owner: 'kernel', reason: '' },
          posix_mqueue: { owner: 'absent', reason: '' },
          sysvipc: { owner: 'module', reason: '' },
          compat32: { owner: 'absent', reason: '' }
        },
        layoutTrusted: true,
        layout: [
          { field: 'task_struct.nsproxy', state: 'ok', live: '1920', build: '1920' },
          { field: 'pid_namespace.level', state: 'ok', live: '72', build: '72' },
          { field: 'seq_file.buf', state: 'unverified', live: '0', build: '8' }
        ],
        hooks: [
          { name: 'create_new_namespaces', route: 'kprobe', state: 'installed' },
          { name: 'copy_namespaces', route: 'none', state: 'refused' },
          { name: 'proc_ns_dir_lookup', route: 'slot', state: 'installed' },
          { name: 'proc_pid_status', route: 'kprobe', state: 'installed' }
        ],
        runtime: {
          ...fullFeatures.runtime,
          criticalHooks: 'degraded',
          degrade: 'yes',
          gate: 'gated',
          hookRoutePolicy: 'kprobe',
          pidnsImpl: baseImpl,
          pidnsBackends: baseBackends,
          mqueueFs: 'off',
          mqueueShim: 'no',
          nspidInsert: 'append',
          slotsIpc64: '12/18',
          slotsIpc32: '0/18'
        }
      }
    };
  }
  if (key === 'untrusted') {
    return {
      meta: {
        release: '6.6.77-android15-0-g12345678',
        kmi: 'android15-6.6',
        pidBackends: baseBackends,
        moddir: '/data/adb/modules/phdroid',
        data: '/data/adb/phdroid',
        auto: false,
        blocked: false,
        insmodMark: false,
        mqueueFs: false
      },
      kmis: ['android15-6.6'],
      kos: ['droid_lkm.ko'],
      loaded: true,
      vendorLoaded: false,
      hasVendorKo: false,
      savedConfigText: 'modules=droid_lkm\ndroid_lkm=degrade=1',
      activeMainParams: { degrade: '1', pid_impl: baseImpl },
      activeVendorParams: {},
      features: {
        ...fullFeatures,
        layoutTrusted: false,
        layout: [
          { field: 'task_struct.nsproxy', state: 'unverified', live: '0', build: '1920' },
          { field: 'task_struct.thread_pid', state: 'ok', live: '1520', build: '1520' },
          { field: 'pid_namespace.level', state: 'ok', live: '72', build: '72' }
        ],
        hooks: [],
        runtime: {
          ...fullFeatures.runtime,
          degrade: 'yes',
          pidns: 'off',
          ipcns: 'off',
          mqueueFs: 'off',
          procNs: 'off',
          nspidStatus: 'off',
          nspidInsert: 'off',
          syscallSlots: 'off',
          slotsClone: 'no',
          slotsIpc64: '0/18',
          slotsIpc32: '0/18'
        }
      }
    };
  }
  if (key === 'multiKmi') {
    return {
      meta: {
        release: '5.10.209-perf-g81a2b3c',
        kmi: '',
        pidBackends: baseBackends,
        moddir: '/data/adb/modules/phdroid',
        data: '/data/adb/phdroid',
        auto: false,
        blocked: false,
        insmodMark: false,
        mqueueFs: false
      },
      kmis: ['android12-5.10', 'android13-5.10'],
      kos: ['droid_lkm.ko'],
      loaded: false,
      vendorLoaded: false,
      hasVendorKo: false,
      savedConfigText: 'modules=droid_lkm',
      activeMainParams: {},
      activeVendorParams: {},
      features: null
    };
  }
  if (key === 'unloaded') {
    return {
      meta: {
        release: '6.12.38-android16-5-g8c67d4274c0a-ab14275539-4k',
        kmi: 'android16-6.12',
        pidBackends: baseBackends,
        moddir: '/data/adb/modules/phdroid',
        data: '/data/adb/phdroid',
        auto: false,
        blocked: false,
        insmodMark: false,
        mqueueFs: false
      },
      kmis: ['android16-6.12'],
      kos: ['droid_lkm.ko', 'droid_lkm_vendor.ko'],
      loaded: false,
      vendorLoaded: false,
      hasVendorKo: true,
      savedConfigText: 'modules=droid_lkm droid_lkm_vendor',
      activeMainParams: {},
      activeVendorParams: {},
      features: null
    };
  }
  return {
    meta: {
      release: '6.12.38-android16-5-g8c67d4274c0a-ab14275539-4k',
      kmi: 'android16-6.12',
      pidBackends: baseBackends,
      moddir: '/data/adb/modules/phdroid',
      data: '/data/adb/phdroid',
      auto: true,
      blocked: true,
      insmodMark: false,
      mqueueFs: false
    },
    kmis: ['android16-6.12'],
    kos: ['droid_lkm.ko', 'droid_lkm_vendor.ko'],
    loaded: false,
    vendorLoaded: false,
    hasVendorKo: true,
    savedConfigText: 'modules=droid_lkm droid_lkm_vendor',
    activeMainParams: {},
    activeVendorParams: {},
    features: null
  };
}

function makeAllCache(flavor: ResolvedFlavor): Record<ScenarioKey, StatusSnapshot> {
  return {
    full: makeScenarioSnapshot('full', flavor),
    degraded: makeScenarioSnapshot('degraded', flavor),
    untrusted: makeScenarioSnapshot('untrusted', flavor),
    multiKmi: makeScenarioSnapshot('multiKmi', flavor),
    unloaded: makeScenarioSnapshot('unloaded', flavor),
    blocked: makeScenarioSnapshot('blocked', flavor)
  };
}

class MockBackend {
  private scenario: ScenarioKey = 'full';
  private flavor: ResolvedFlavor =
    getCompiledFlavor() === 'auto' ? 'standard' : (getCompiledFlavor() as ResolvedFlavor);
  private cache: Record<ScenarioKey, StatusSnapshot> = makeAllCache(this.flavor);
  private moduleLogs: string[] = [
    '11-21 00:12:04 [boot] insmod droid_lkm params=[] rc=0',
    '11-21 00:12:04 [boot] insmod droid_lkm_vendor params=[] rc=0',
    '11-21 00:12:04 [boot] loaded droid_lkm droid_lkm_vendor',
    '11-21 00:18:30 [ctl] config saved'
  ];
  private dmesgLogs: string[] = [
    '[   12.401120] [I] [droid_lkm] params: verbose=0 skip_sysvipc=0 no_fake_ns=0',
    '[   12.415802] [I] [droid_lkm] caps: kernel=6.12.38 idmap=1 vm_flags=1 ns_count=1 ctl_table=0',
    '[   12.415810] [I] [droid_lkm] features: ipcns=module mqueue=module sysvipc=module compat32=kernel',
    '[   12.420014] [I] [droid_lkm/hook] policy: route=0 degrade=0',
    '[   12.423901] [I] [droid_lkm] pidns ready (reaper=412)',
    '[   12.429112] [I] [droid_lkm] clone slots patched: clone=220 clone3=435',
    '[   12.431005] [I] [droid_lkm] ready',
    '[   12.441200] [I] [droid_lkm_vendor] ghost: find_task_by_vpid hooked, match=oplus_',
    '[   12.441980] [I] [droid_lkm_vendor] ready',
    "[   14.100211] init: Service 'zygote' started",
    '[   18.550102] binder: 1204:1204 transaction failed 29189/-22'
  ];

  setScenario(next: ScenarioKey) {
    this.scenario = next;
  }

  getScenario(): ScenarioKey {
    return this.scenario;
  }

  setFlavor(next: ResolvedFlavor) {
    this.flavor = next;
    this.cache = makeAllCache(next);
  }

  getFlavor(): ResolvedFlavor {
    return this.flavor;
  }

  getStatus(): StatusSnapshot {
    return structuredClone(this.cache[this.scenario]);
  }

  setAuto(enable: boolean) {
    this.cache[this.scenario].meta.auto = enable;
    this.moduleLogs.push(`11-21 00:49:00 [ctl] ${enable ? 'auto enabled' : 'auto disabled'}`);
  }

  unblock() {
    this.cache[this.scenario].meta.blocked = false;
    this.moduleLogs.push('11-21 00:49:10 [ctl] block cleared');
  }

  saveConfig(text: string) {
    this.cache[this.scenario].savedConfigText = text;
    this.moduleLogs.push('11-21 00:50:00 [ctl] config saved');
  }

  install(overrideConf?: string) {
    const snap = this.cache[this.scenario];
    const conf = parseSavedConfig(overrideConf || snap.savedConfigText);
    const backends = snap.meta.pidBackends;
    const defImpl: 'main' | 'native' = backends === 'native' ? 'native' : 'main';

    const defMain: Record<string, string> = {};
    PARAMS_SCHEMA.common.concat(PARAMS_SCHEMA.diag).forEach((i) => {
      defMain[i.key] = i.key === 'pid_impl' ? defImpl : i.def;
    });
    const defVendor: Record<string, string> = {};
    PARAMS_SCHEMA.vendor.forEach((i) => {
      defVendor[i.key] = i.def;
    });

    const parsedMain = kvToObj(conf.droid_lkm, defMain);
    if (backends === 'native') parsedMain.pid_impl = 'native';
    if (backends === 'main') parsedMain.pid_impl = 'main';
    const activeImpl: 'main' | 'native' = parsedMain.pid_impl === 'native' ? 'native' : 'main';

    snap.loaded = conf.modules.includes('droid_lkm');
    snap.vendorLoaded = snap.hasVendorKo && conf.modules.includes('droid_lkm_vendor');
    snap.activeMainParams = parsedMain;
    snap.activeVendorParams = kvToObj(conf.droid_lkm_vendor, defVendor);
    snap.features = snap.loaded ? makeFullFeatures(backends, activeImpl) : null;
    if (snap.loaded) {
      const kmiNote = conf.kmi ? ` (${conf.kmi})` : '';
      this.moduleLogs.push(
        `11-21 00:49:20 [manual] insmod droid_lkm${kmiNote} params=[${conf.droid_lkm}] rc=0`
      );
    }
    if (snap.vendorLoaded) {
      this.moduleLogs.push(
        `11-21 00:49:20 [manual] insmod droid_lkm_vendor params=[${conf.droid_lkm_vendor}] rc=0`
      );
    }
  }

  getLogs(kind: LogKind): string[] {
    return kind === 'module' ? [...this.moduleLogs] : [...this.dmesgLogs];
  }

  clearModuleLogs() {
    this.moduleLogs = [];
  }
}

export const mockBackend = new MockBackend();
