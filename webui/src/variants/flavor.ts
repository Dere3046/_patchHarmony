// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

import { PARAMS_SCHEMA } from '../i18n';
import type {
  BuildFlavor,
  FlavorCapabilities,
  ParamSpec,
  PidBackendMask,
  ResolvedFlavor,
  StatusSnapshot
} from '../types';

export function getCompiledFlavor(): BuildFlavor {
  return typeof __PHDROID_FLAVOR__ === 'string' ? __PHDROID_FLAVOR__ : 'auto';
}

export function isBuildLockedFlavor(): boolean {
  return getCompiledFlavor() !== 'auto';
}

export function resolveEffectiveFlavor(
  previewFlavor: ResolvedFlavor,
  nativeHost: boolean,
  snap: StatusSnapshot | null
): ResolvedFlavor {
  const compiled = getCompiledFlavor();
  if (compiled !== 'auto') {
    return compiled;
  }
  if (!nativeHost) {
    return previewFlavor;
  }
  if (snap) {
    if (snap.meta.pidBackends === 'native') return 'native';
    if (snap.meta.pidBackends === 'both') return 'main-native';
  }
  return 'standard';
}

export function getFlavorCapabilities(
  flavor: ResolvedFlavor,
  snap?: StatusSnapshot | null
): FlavorCapabilities {
  if (flavor === 'native') {
    return { flavor: 'native', backends: 'native' };
  }
  if (flavor === 'main-native') {
    return { flavor: 'main-native', backends: 'both' };
  }
  const detected =
    snap?.meta.pidBackends && snap.meta.pidBackends !== 'unknown'
      ? snap.meta.pidBackends
      : 'main';
  return {
    flavor: 'standard',
    backends: getCompiledFlavor() === 'standard' ? 'main' : detected
  };
}

export function effectivePidImplDefault(backends: PidBackendMask): 'main' | 'native' {
  return backends === 'native' ? 'native' : 'main';
}

export function effectiveMainDefaults(backends: PidBackendMask): Record<string, string> {
  const out: Record<string, string> = {};
  PARAMS_SCHEMA.common.concat(PARAMS_SCHEMA.diag).forEach((item) => {
    out[item.key] = item.key === 'pid_impl' ? effectivePidImplDefault(backends) : item.def;
  });
  return out;
}

export function harmonizeMainParams(
  store: Record<string, string>,
  backends: PidBackendMask
): Record<string, string> {
  const next = { ...store };
  if (backends === 'main') {
    next.pid_impl = 'main';
    next.nsproxy = '0';
  } else if (backends === 'native') {
    next.pid_impl = 'native';
  } else if (next.pid_impl !== 'native') {
    next.nsproxy = '0';
  }
  return next;
}

export function serializeWithEffectiveDefaults(
  store: Record<string, string>,
  schema: ParamSpec[],
  defaults: Record<string, string>
): string {
  const out: string[] = [];
  schema.forEach((item) => {
    const defVal = defaults[item.key] ?? item.def;
    const val = store[item.key] ?? defVal;
    if (val !== defVal && val !== '') {
      out.push(`${item.key}=${val}`);
    }
  });
  return out.join(' ');
}
