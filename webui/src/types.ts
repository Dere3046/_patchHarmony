// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

export type Lang = 'zh' | 'en';
export type TabKey = 'overview' | 'config' | 'logs';
export type LogKind = 'module' | 'dmesg';
export type ThemeMode = 'auto' | 'light' | 'dark';
export type PidBackendMask = 'main' | 'native' | 'both' | 'unknown';
export type BuildFlavor = 'auto' | 'standard' | 'native' | 'main-native';
export type ResolvedFlavor = 'standard' | 'native' | 'main-native';
export type ScenarioKey = 'full' | 'degraded' | 'untrusted' | 'multiKmi' | 'unloaded' | 'blocked';

export interface FlavorCapabilities {
  flavor: ResolvedFlavor;
  backends: PidBackendMask;
}

export type ParamType = 'bool' | 'enum' | 'str' | 'num';

export interface ParamSpec {
  key: string;
  type: ParamType;
  def: string;
  opts?: Record<Lang, [string, string][]>;
  label: Record<Lang, string>;
  desc: Record<Lang, string>;
}

export interface ExtraParam {
  id: number;
  mod: 'droid_lkm' | 'droid_lkm_vendor';
  key: string;
  val: string;
}

export interface FeatureCapability {
  owner: 'module' | 'kernel' | 'absent' | string;
  reason: string;
}

export interface LayoutEntry {
  field: string;
  state: string;
  live: string;
  build: string;
}

export interface HookEntry {
  name: string;
  route: string;
  state: string;
}

export interface RuntimeInfo {
  moduleReady: string;
  criticalHooks: string;
  degrade: string;
  gate: string;
  hookRoutePolicy: string;
  pidns: string;
  pidnsImpl: string;
  pidnsBackends: string;
  nsproxy: string;
  ipcns: string;
  mqueueFs: string;
  mqueueShim: string;
  procNs: string;
  nspidStatus: string;
  nspidInsert: string;
  syscallSlots: string;
  slotsClone: string;
  slotsIpc64: string;
  slotsIpc32: string;
}

export interface FeaturesData {
  capabilities: {
    ipc_ns: FeatureCapability;
    posix_mqueue: FeatureCapability;
    sysvipc: FeatureCapability;
    compat32: FeatureCapability;
  };
  layoutTrusted: boolean;
  layout: LayoutEntry[];
  hooks: HookEntry[];
  runtime: RuntimeInfo;
}

export interface StatusMeta {
  release: string;
  kmi: string;
  pidBackends: PidBackendMask;
  moddir: string;
  data: string;
  auto: boolean;
  blocked: boolean;
  insmodMark: boolean;
  mqueueFs: boolean;
}

export interface StatusSnapshot {
  meta: StatusMeta;
  kmis: string[];
  kos: string[];
  loaded: boolean;
  vendorLoaded: boolean;
  hasVendorKo: boolean;
  savedConfigText: string;
  activeMainParams: Record<string, string>;
  activeVendorParams: Record<string, string>;
  features: FeaturesData | null;
}

export interface SavedConfigMap {
  kmi: string;
  modules: string[];
  droid_lkm: string;
  droid_lkm_vendor: string;
  raw: Record<string, string>;
}
