export type Tab = 'dashboard' | 'map' | 'settings' | 'network' | 'diagnostics' | 'assistant';

export type ConnectionState = 'connecting' | 'online' | 'offline';

export type ToastKind = 'ok' | 'error' | 'info';

export interface Toast {
  id: number;
  text: string;
  kind: ToastKind;
}

/** Кадр `message`: полная телеметрия блока (см. WEB_SOCKET_FUNCS.h). */
export interface Telemetry {
  micros: number;
  select_cart: number;
  select_qs: number;
  aa: number;
  qs: number;
  cc: number;
  modulator: number;
  max_rpm: number;
  max_rpm_angle_kiker: number;
  advance_angle: number;
  rpm: number;
  termo: number;
  throttle: number;
  init_angle: number;
  filter_val: number;
  high: number;
  init_flash_time: number;
  cpu_termo: number;
  card: number;
  wsid: string;
  wpass: string;
  INVERT_SENSOR: number;
  INVERT_THROTTLE: number;
  servo: number;
  servo_position: number;
  servo_type: number;
  servo_rpm: number;
  servo_percent: number;
  type: number;
  wifi_auto_update_enabled: number;
  wifi_update_ssid: string;
  wifi_update_status: string;
  wifi_last_check_time: number;
  wifi_last_attempt_time: number;
  wifi_connected: number;
  wifi_password_set: number;
  firmware_version: string;
  ota_update_available: number;
  ota_last_version: string;
  motor_running: number;
  angles: number[][];
}

export interface Sample {
  t: number;
  rpm: number;
  throttle: number;
}

export interface MapPoint {
  cart: number;
  point: number;
  rpm: number;
  angle: number;
  aa: number;
  qa: number;
  servo: number;
}

export interface WifiNetwork {
  ssid: string;
  rssi: number;
  secure: boolean;
}

export interface WifiState {
  auto: boolean;
  ssid: string;
  status: string;
  passwordSet: boolean;
  connected: boolean;
  lastCheck: number;
  lastAttempt: number;
}

export interface DeviceSettings {
  maxRpm: number;
  maxRpmAngleKicker: number;
  initAngle: number;
  initFlashTime: number;
  filter: number;
  angleAdvance: boolean;
  quickShifter: boolean;
  cartsSwitching: boolean;
  highOutput: boolean;
  invertSensor: boolean;
  invertThrottle: boolean;
  servo: boolean;
  servoType: number;
  servoRpm: number;
  servoPercent: number;
  type: number;
}

export interface DeviceInfo {
  ota_update_available: number;
  ota_last_version: string;
  firmware_version: string;
  diag_wifi_enabled: number;
}

/** Ответ `/api/status` (обычный HTTP или HTTPS). */
export interface DeviceStatus {
  host?: string;
  firmware?: string;
  tls?: boolean;
  cert?: boolean;
  certExpiry?: string;
  certDaysLeft?: number;
  wifiConnected?: boolean;
  wifiStatus?: string;
  active_cart?: number;
  rpm?: number;
  pairCode?: string;
  mac?: string;
  paired?: boolean;
}

/** Ответ `/status`. */
export interface SysStatus {
  wifi: {
    mode: string;
    ap_ssid: string;
    sta_ssid: string;
    sta_connected: boolean;
    status: string;
    last_check_time: number;
    last_attempt_time: number;
    next_check_ms: number;
  };
  ota: {
    currentVersion: string;
    mode: string;
    initialized: boolean;
    updateMode: boolean;
    updateAvailable: boolean;
    lastFirmwareInfo: {
      version: string;
      size: string;
      md5: string;
      sha256: string;
      changelog: string;
    };
  };
  motor: {
    rpm: number;
    throttle: number;
    motor_running: boolean;
  };
  config: {
    autoUpdateEnabled: boolean;
    wifiUpdateSSID: string;
    wifiPasswordSet: boolean;
    wifiConnected: boolean;
    retryIntervalMs: number;
    nextCheckMs: number;
  };
}

export interface OtaCheck {
  updateAvailable: boolean;
  currentVersion: string;
  mode: string;
  newVersion?: string;
  size?: number;
  md5?: string;
  sha256?: string;
  changelog?: string;
}

export interface MarkScan {
  state: 'off' | 'scanning' | 'refining' | 'done' | 'cancelled';
  method: string;
  candidate: number;
  attempt: number;
  total: number;
  rpm: number;
  cranking_rpm: number;
  caught: number;
  best: number;
  init_angle: number;
  note: string;
}

export interface UploadResult {
  ok: boolean;
  text: string;
}

export const CART_PRESETS = [
  { id: 0, label: 'Пользовательский (ровный график)' },
  { id: 1, label: 'YX 125' },
  { id: 2, label: 'YX 140' },
  { id: 3, label: 'SmartCom' },
  { id: 4, label: '166/172 FMM' },
  { id: 5, label: 'ZS 190' },
  { id: 6, label: '175 FMM' },
] as const;

export const MAP_GROUPS = [
  { id: 'start', label: 'Старт', from: 0, to: 0 },
  { id: 'idle', label: 'Холостой ход', from: 1, to: 2 },
  { id: 'accel', label: 'Разгон', from: 3, to: 6 },
  { id: 'rest', label: 'Остальной ход', from: 7, to: 21 },
] as const;

export function cartPoints(type: number): number {
  return type === 0 ? 7 : type === 1 ? 12 : 22;
}

export function plural(count: number, one: string, few: string, many: string): string {
  const absolute = Math.abs(count) % 100;
  const last = absolute % 10;
  if (absolute > 10 && absolute < 20) return many;
  if (last === 1) return one;
  if (last >= 2 && last <= 4) return few;
  return many;
}

export function pointsLabel(count: number): string {
  return `${count} ${plural(count, 'точка', 'точки', 'точек')}`;
}

export function settingsFromTelemetry(t: Telemetry): DeviceSettings {
  return {
    maxRpm: t.max_rpm,
    maxRpmAngleKicker: t.max_rpm_angle_kiker,
    initAngle: t.init_angle,
    initFlashTime: t.init_flash_time,
    filter: t.filter_val,
    angleAdvance: !!t.aa,
    quickShifter: !!t.qs,
    cartsSwitching: !!t.cc,
    highOutput: !!t.high,
    invertSensor: !!t.INVERT_SENSOR,
    invertThrottle: !!t.INVERT_THROTTLE,
    servo: !!t.servo,
    servoType: t.servo_type,
    servoRpm: t.servo_rpm,
    servoPercent: t.servo_percent,
    type: t.type,
  };
}

export function mapWifiState(data: Record<string, unknown>): WifiState {
  return {
    auto: Number(data['wifi_auto_update_enabled'] ?? 0) === 1,
    ssid: String(data['wifi_update_ssid'] ?? ''),
    status: String(data['wifi_update_status'] ?? ''),
    passwordSet: Number(data['wifi_password_set'] ?? 0) === 1,
    connected: Number(data['wifi_connected'] ?? 0) === 1,
    lastCheck: Number(data['wifi_last_check_time'] ?? 0),
    lastAttempt: Number(data['wifi_last_attempt_time'] ?? 0),
  };
}

export function normalizeNetworks(raw: unknown): WifiNetwork[] {
  if (!Array.isArray(raw)) {
    return [];
  }

  const networks: WifiNetwork[] = [];

  for (const item of raw) {
    if (Array.isArray(item)) {
      networks.push({
        ssid: String(item[0] ?? ''),
        rssi: Number(item[1] ?? -100),
        secure: Number(item[2] ?? 0) === 1,
      });
      continue;
    }

    if (item && typeof item === 'object') {
      const source = item as Record<string, unknown>;
      networks.push({
        ssid: String(source['ssid'] ?? ''),
        rssi: Number(source['rssi'] ?? -100),
        secure: source['secure'] === true || Number(source['secure'] ?? 0) === 1,
      });
    }
  }

  return networks.filter((network) => network.ssid.length > 0);
}

export function wifiStatusLabel(status: string): string {
  switch (status) {
    case 'ap_ready':
      return 'точка доступа готова';
    case 'connected':
      return 'подключено';
    case 'connect_failed':
      return 'не удалось подключиться';
    case 'idle':
      return 'ожидание';
    case 'checking':
      return 'проверяю обновления';
    case 'updating':
      return 'установка обновления';
    case 'no_credentials':
      return 'сеть не задана';
    default:
      return status || '—';
  }
}

export function signalBars(rssi: number): number {
  if (rssi >= -55) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -78) return 2;
  return 1;
}

export function activeCart(t: Telemetry): number {
  const selected = Number(t.select_cart);
  if (selected >= 1 && selected <= 3) {
    return selected;
  }
  return Math.min(3, Math.max(1, Number(t.card) + 1));
}
