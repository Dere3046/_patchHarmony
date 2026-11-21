// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

import { useEffect, useRef, type CSSProperties, type ReactNode, type RefObject } from 'react';

function useElementProp<T extends HTMLElement, K extends keyof T>(
  ref: RefObject<T | null>,
  key: K,
  value: T[K]
) {
  useEffect(() => {
    const el = ref.current;
    if (el && el[key] !== value) {
      el[key] = value;
    }
  }, [ref, key, value]);
}

function useCustomEvent<T extends HTMLElement>(
  ref: RefObject<T | null>,
  eventName: string,
  handler: (el: T) => void
) {
  const handlerRef = useRef(handler);
  handlerRef.current = handler;

  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    const listener = () => handlerRef.current(el);
    el.addEventListener(eventName, listener);
    return () => el.removeEventListener(eventName, listener);
  }, [ref, eventName]);
}

interface MduiSwitchProps {
  checked: boolean;
  disabled?: boolean;
  onChange: (checked: boolean) => void;
}

export function MduiSwitch({ checked, disabled, onChange }: MduiSwitchProps) {
  const ref = useRef<HTMLElement & { checked: boolean; disabled: boolean }>(null);
  useElementProp(ref, 'checked', checked);
  useElementProp(ref, 'disabled', Boolean(disabled));
  useCustomEvent(ref, 'change', (el) => onChange(Boolean(el.checked)));

  return <mdui-switch ref={ref as never} />;
}

interface MduiTabsProps {
  value: string;
  variant?: 'primary' | 'secondary';
  fullWidth?: boolean;
  onChange: (value: string) => void;
  children: ReactNode;
}

export function MduiTabs({ value, variant = 'secondary', fullWidth = true, onChange, children }: MduiTabsProps) {
  const ref = useRef<HTMLElement & { value?: string }>(null);
  useElementProp(ref, 'value', value);
  useCustomEvent(ref, 'change', (el) => {
    if (el.value) onChange(el.value);
  });

  return (
    <mdui-tabs ref={ref as never} variant={variant} full-width={fullWidth} value={value}>
      {children}
    </mdui-tabs>
  );
}

interface MduiSegmentedGroupProps {
  value: string;
  onChange: (value: string) => void;
  children: ReactNode;
}

export function MduiSegmentedGroup({ value, onChange, children }: MduiSegmentedGroupProps) {
  const ref = useRef<HTMLElement & { value?: string | string[] }>(null);
  const valueRef = useRef(value);
  valueRef.current = value;

  useElementProp(ref, 'value', value);

  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    // Prevent MDUI single-select group from toggling the active option back to empty
    const blockDeselect = (event: Event) => {
      const target = event.target as HTMLElement | null;
      const item = target?.closest('mdui-segmented-button') as
        | (HTMLElement & { value?: string; disabled?: boolean; selected?: boolean })
        | null;
      if (item && !item.disabled && (item.value === valueRef.current || item.selected)) {
        event.preventDefault();
        event.stopPropagation();
      }
    };
    el.addEventListener('click', blockDeselect, true);
    return () => el.removeEventListener('click', blockDeselect, true);
  }, []);

  useCustomEvent(ref, 'change', (el) => {
    const next = Array.isArray(el.value) ? el.value[0] : el.value;
    if (!next) {
      queueMicrotask(() => {
        el.value = valueRef.current;
      });
      return;
    }
    if (next !== valueRef.current) {
      onChange(next);
    }
  });

  return (
    <mdui-segmented-button-group ref={ref as never} selects="single" value={value}>
      {children}
    </mdui-segmented-button-group>
  );
}

interface MduiFilterChipProps {
  selected: boolean;
  visible?: boolean;
  onToggle: (selected: boolean) => void;
  children: ReactNode;
}

export function MduiFilterChip({ selected, visible = true, onToggle, children }: MduiFilterChipProps) {
  const ref = useRef<HTMLElement & { selected: boolean }>(null);
  useElementProp(ref, 'selected', selected);
  useCustomEvent(ref, 'change', (el) => onToggle(Boolean(el.selected)));

  const style: CSSProperties | undefined = visible ? undefined : { display: 'none' };

  return (
    <mdui-chip ref={ref as never} variant="filter" selectable style={style}>
      {children}
    </mdui-chip>
  );
}

interface MduiDialogProps {
  open: boolean;
  headline: string;
  onClose: () => void;
  children: ReactNode;
}

export function MduiDialog({ open, headline, onClose, children }: MduiDialogProps) {
  const ref = useRef<HTMLElement & { open: boolean }>(null);
  useElementProp(ref, 'open', open);
  useCustomEvent(ref, 'closed', () => onClose());

  return (
    <mdui-dialog ref={ref as never} headline={headline} close-on-esc close-on-overlay-click>
      {children}
    </mdui-dialog>
  );
}
