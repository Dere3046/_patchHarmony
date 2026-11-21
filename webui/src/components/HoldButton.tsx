// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

import { useEffect, useRef, useState, type KeyboardEvent as ReactKeyboardEvent, type PointerEvent as ReactPointerEvent } from 'react';

interface HoldButtonProps {
  label: string;
  holdingLabel: string;
  busyLabel: string;
  doneLabel: string;
  holdMs?: number;
  disabled?: boolean;
  busy?: boolean;
  done?: boolean;
  pulseKey?: number;
  onConfirm: () => void;
  onShortTap?: () => void;
}

export function HoldButton({
  label,
  holdingLabel,
  busyLabel,
  doneLabel,
  holdMs = 650,
  disabled = false,
  busy = false,
  done = false,
  pulseKey = 0,
  onConfirm,
  onShortTap
}: HoldButtonProps) {
  const btnRef = useRef<HTMLButtonElement>(null);
  const rafRef = useRef<number | null>(null);
  const activeRef = useRef(false);
  const pointerIdRef = useRef<number | null>(null);
  const startPosRef = useRef({ x: 0, y: 0 });
  const startTimeRef = useRef(0);
  const committedRef = useRef(false);
  const [holding, setHolding] = useState(false);

  const setProgress = (p: number) => {
    const el = btnRef.current;
    if (el) el.style.setProperty('--hold-p', p.toFixed(4));
  };

  const stopLoop = () => {
    if (rafRef.current !== null) {
      cancelAnimationFrame(rafRef.current);
      rafRef.current = null;
    }
  };

  useEffect(() => {
    return () => stopLoop();
  }, []);

  const cancelHold = (notifyShortTap: boolean) => {
    if (!activeRef.current) return;
    activeRef.current = false;
    pointerIdRef.current = null;
    stopLoop();
    setHolding(false);
    setProgress(0);
    if (notifyShortTap && !committedRef.current) {
      onShortTap?.();
    }
  };

  const handlePointerDown = (e: ReactPointerEvent<HTMLButtonElement>) => {
    if (disabled || busy || done) return;
    if (e.button !== 0 && e.pointerType === 'mouse') return;

    try {
      e.currentTarget.setPointerCapture(e.pointerId);
    } catch {
      // ignore synthetic events
    }

    activeRef.current = true;
    committedRef.current = false;
    pointerIdRef.current = e.pointerId;
    startPosRef.current = { x: e.clientX, y: e.clientY };
    startTimeRef.current = performance.now();
    setHolding(true);

    if (typeof navigator.vibrate === 'function') {
      navigator.vibrate(6);
    }

    const tick = (now: number) => {
      if (!activeRef.current) return;
      const elapsed = now - startTimeRef.current;
      const p = Math.min(1, elapsed / holdMs);
      setProgress(p);
      if (p >= 1) {
        committedRef.current = true;
        activeRef.current = false;
        pointerIdRef.current = null;
        rafRef.current = null;
        setHolding(false);
        setProgress(0);
        if (typeof navigator.vibrate === 'function') {
          navigator.vibrate([14, 30, 18]);
        }
        onConfirm();
        return;
      }
      rafRef.current = requestAnimationFrame(tick);
    };

    stopLoop();
    rafRef.current = requestAnimationFrame(tick);
  };

  const handlePointerMove = (e: ReactPointerEvent<HTMLButtonElement>) => {
    if (!activeRef.current || pointerIdRef.current !== e.pointerId) return;
    const dx = e.clientX - startPosRef.current.x;
    const dy = e.clientY - startPosRef.current.y;
    if (Math.hypot(dx, dy) > 28) {
      cancelHold(false);
    }
  };

  const handlePointerUp = (e: ReactPointerEvent<HTMLButtonElement>) => {
    if (pointerIdRef.current !== e.pointerId) return;
    cancelHold(true);
  };

  const handlePointerCancel = (e: ReactPointerEvent<HTMLButtonElement>) => {
    if (pointerIdRef.current !== e.pointerId) return;
    cancelHold(false);
  };

  const handleKeyDown = (e: ReactKeyboardEvent<HTMLButtonElement>) => {
    if (disabled || busy || done) return;
    if ((e.key === 'Enter' || e.key === ' ') && !e.repeat) {
      e.preventDefault();
      onConfirm();
    }
  };

  const cls = [
    'hold-btn',
    holding ? 'holding' : '',
    done ? 'done' : '',
    done && pulseKey > 0 ? 'just-done' : ''
  ]
    .filter(Boolean)
    .join(' ');

  const text = busy ? busyLabel : done ? doneLabel : holding ? holdingLabel : label;

  return (
    <button
      ref={btnRef}
      type="button"
      className={cls}
      disabled={disabled || busy || done}
      onPointerDown={handlePointerDown}
      onPointerMove={handlePointerMove}
      onPointerUp={handlePointerUp}
      onPointerCancel={handlePointerCancel}
      onKeyDown={handleKeyDown}
    >
      <span className="hold-fill" aria-hidden="true" />
      <span className="hold-content">
        {done && (
          <svg
            key={pulseKey}
            className="hold-check"
            width="15"
            height="15"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="2.8"
            strokeLinecap="round"
            strokeLinejoin="round"
            aria-hidden="true"
          >
            <path d="M5 13l4 4L19 7" />
          </svg>
        )}
        <span>{text}</span>
      </span>
    </button>
  );
}
