import { Injectable, computed, signal } from '@angular/core';
import {
  DeviceInfo,
  DeviceSettings,
  DeviceStatus,
  MapPoint,
  MarkScan,
  OtaCheck,
  Sample,
  SysStatus,
  Telemetry,
  Toast,
  ToastKind,
  UploadResult,
  WifiNetwork,
  WifiState,
  mapWifiState,
  normalizeNetworks,
  settingsFromTelemetry,
} from './models';

type ConnectionState = 'connecting' | 'online' | 'offline';

interface Pending {
  resolve: (value: unknown) => void;
  reject: (error: Error) => void;
  timer: ReturnType<typeof setTimeout>;
}

interface Command {
  event: string;
  data?: unknown;
}

const HISTORY_LIMIT = 120;
const DEVICE_KEY_STORAGE = 'ded.core.deviceKey';

@Injectable({ providedIn: 'root' })
export class DeviceService {
  readonly telemetry = signal<Telemetry | null>(null);
  readonly info = signal<DeviceInfo | null>(null);
  readonly connection = signal<ConnectionState>('connecting');
  readonly history = signal<Sample[]>([]);
  readonly wifiState = signal<WifiState | null>(null);
  readonly wifiNetworks = signal<WifiNetwork[]>([]);
  readonly scanning = signal(false);
  readonly deviceStatus = signal<DeviceStatus | null>(null);
  readonly sysStatus = signal<SysStatus | null>(null);
  readonly otaCheck = signal<OtaCheck | null>(null);
  readonly markScan = signal<MarkScan | null>(null);
  readonly toasts = signal<Toast[]>([]);
  readonly deviceKey = signal<string>(readDeviceKey());

  readonly settings = computed<DeviceSettings | null>(() => {
    const frame = this.telemetry();
    return frame ? settingsFromTelemetry(frame) : null;
  });

  readonly firmwareVersion = computed(
    () => this.telemetry()?.firmware_version || this.deviceStatus()?.firmware || this.info()?.firmware_version || '—',
  );

  readonly online = computed(() => this.connection() === 'online');

  /** HTTPS-страница блока: часть HTTP-маршрутов на 443 не зарегистрирована. */
  readonly secure = location.protocol === 'https:';
  readonly httpApi = !this.secure;

  private socket: WebSocket | null = null;
  private reconnectDelay = 1000;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private pingTimer: ReturnType<typeof setInterval> | null = null;
  private pending = new Map<string, Pending>();
  private toastId = 0;
  private closedByUser = false;

  connect(): void {
    this.closedByUser = false;

    if (this.socket && (this.socket.readyState === WebSocket.OPEN || this.socket.readyState === WebSocket.CONNECTING)) {
      return;
    }

    this.connection.set('connecting');

    let socket: WebSocket;
    try {
      socket = new WebSocket(this.webSocketUrl());
    } catch {
      this.connection.set('offline');
      this.scheduleReconnect();
      return;
    }

    this.socket = socket;

    socket.onopen = () => {
      this.reconnectDelay = 1000;
      this.connection.set('online');
      this.startPing();
      this.send({ event: 'settings', data: {} });
      void this.refreshStatus();
    };

    socket.onmessage = (event: MessageEvent) => this.handleFrame(String(event.data));

    socket.onerror = () => {
      // Браузер вслед за ошибкой закроет сокет — переподключение делает onclose.
    };

    socket.onclose = () => {
      this.stopPing();
      this.rejectAll('Связь с блоком прервана');
      this.connection.set('offline');
      this.socket = null;

      if (!this.closedByUser) {
        this.scheduleReconnect();
      }
    };
  }

  disconnect(): void {
    this.closedByUser = true;
    this.stopPing();
    this.clearReconnect();
    this.socket?.close();
    this.socket = null;
    this.connection.set('offline');
  }

  private webSocketUrl(): string {
    // ?ws=ws://host:port/ — удобно для локальной проверки интерфейса без блока.
    const override = new URLSearchParams(location.search).get('ws');
    if (override) {
      return override;
    }

    if (location.protocol === 'https:') {
      return `wss://${location.host}/ws`;
    }

    return `ws://${location.hostname}:81/`;
  }

  private scheduleReconnect(): void {
    this.clearReconnect();
    this.reconnectTimer = setTimeout(() => {
      this.reconnectDelay = Math.min(this.reconnectDelay * 1.6, 10000);
      this.connect();
    }, this.reconnectDelay);
  }

  private clearReconnect(): void {
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  private startPing(): void {
    this.stopPing();
    this.pingTimer = setInterval(() => {
      if (this.online()) {
        this.send({ event: 'ping' });
      }
    }, 20000);
  }

  private stopPing(): void {
    if (this.pingTimer !== null) {
      clearInterval(this.pingTimer);
      this.pingTimer = null;
    }
  }

  private send(command: Command): boolean {
    const socket = this.socket;
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      return false;
    }

    socket.send(JSON.stringify(command));
    return true;
  }

  private request<T>(command: Command, key: string, timeoutMs = 4000): Promise<T> {
    if (!this.online()) {
      return Promise.reject(new Error('Нет связи с блоком'));
    }

    // Подписка ставится до отправки: быстрый ответ не должен потеряться.
    const promise = this.waitFor<T>(key, timeoutMs);
    if (!this.send(command)) {
      this.rejectPending(key, 'Нет связи с блоком');
    }

    return promise;
  }

  private waitFor<T>(key: string, timeoutMs: number, hint = 'Блок не ответил'): Promise<T> {
    return new Promise<T>((resolve, reject) => {
      const existing = this.pending.get(key);
      if (existing) {
        clearTimeout(existing.timer);
        existing.reject(new Error(hint));
      }

      const timer = setTimeout(() => {
        this.pending.delete(key);
        reject(new Error(hint));
      }, timeoutMs);

      this.pending.set(key, {
        resolve: (value) => resolve(value as T),
        reject,
        timer,
      });
    });
  }

  private resolvePending(key: string, value: unknown): void {
    const pending = this.pending.get(key);
    if (!pending) {
      return;
    }

    clearTimeout(pending.timer);
    this.pending.delete(key);
    pending.resolve(value);
  }

  private rejectAll(reason: string): void {
    for (const [key, pending] of this.pending) {
      clearTimeout(pending.timer);
      pending.reject(new Error(reason));
      this.pending.delete(key);
    }
  }

  private rejectPending(key: string, reason: string): void {
    const pending = this.pending.get(key);
    if (!pending) {
      return;
    }

    clearTimeout(pending.timer);
    this.pending.delete(key);
    pending.reject(new Error(reason));
  }

  private handleFrame(raw: string): void {
    let frame: { event?: string; data?: unknown };
    try {
      frame = JSON.parse(raw) as { event?: string; data?: unknown };
    } catch {
      return;
    }

    const data = frame.data;

    switch (frame.event) {
      case 'message': {
        const telemetry = data as Telemetry;
        if (!telemetry || typeof telemetry.rpm !== 'number') {
          return;
        }

        this.telemetry.set(telemetry);
        this.wifiState.set({
          auto: !!telemetry.wifi_auto_update_enabled,
          ssid: telemetry.wifi_update_ssid ?? '',
          status: telemetry.wifi_update_status ?? '',
          passwordSet: !!telemetry.wifi_password_set,
          connected: !!telemetry.wifi_connected,
          lastCheck: telemetry.wifi_last_check_time ?? 0,
          lastAttempt: telemetry.wifi_last_attempt_time ?? 0,
        });

        const sample: Sample = { t: Date.now(), rpm: telemetry.rpm, throttle: telemetry.throttle };
        this.history.update((list) => (list.length >= HISTORY_LIMIT ? [...list.slice(1), sample] : [...list, sample]));
        break;
      }

      case 'wifi_status':
      case 'wifi': {
        if (data && typeof data === 'object') {
          this.wifiState.set(mapWifiState(data as Record<string, unknown>));
        }
        this.resolvePending('wifi', data);
        break;
      }

      case 'device_info': {
        if (data && typeof data === 'object') {
          this.info.set(data as DeviceInfo);
        }
        break;
      }

      case 'wifi_networks': {
        this.wifiNetworks.set(normalizeNetworks(data));
        this.scanning.set(false);
        this.resolvePending('wifi_networks', data);
        break;
      }

      case 'map_point': {
        const point = data as MapPoint;
        if (point && typeof point.point === 'number') {
          this.resolvePending(`map_point:${point.cart}:${point.point}`, point);
        }
        break;
      }

      case 'map_set': {
        const saved = data as { cart?: number; point?: number; error?: string } | undefined;
        if (saved?.error) {
          for (const key of [...this.pending.keys()]) {
            if (key.startsWith('map_set:')) {
              const pending = this.pending.get(key);
              if (pending) {
                clearTimeout(pending.timer);
                this.pending.delete(key);
                pending.reject(new Error(saved.error));
              }
            }
          }
          break;
        }
        if (saved && typeof saved.point === 'number') {
          this.resolvePending(`map_set:${saved.cart}:${saved.point}`, saved);
        }
        break;
      }

      case 'settings':
      case 'settings_flags': {
        this.resolvePending('settings', data);
        break;
      }

      case 'ap': {
        this.resolvePending('ap', data);
        break;
      }

      case 'wifi_scan': {
        this.resolvePending('wifi_scan', data);
        break;
      }

      case 'mark_scan': {
        if (data && typeof data === 'object') {
          this.markScan.set(data as MarkScan);
        }
        break;
      }

      case 'pong':
      case 'reboot':
      case 'diag':
      case 'servo_error': {
        this.resolvePending(frame.event, data);
        break;
      }

      default:
        break;
    }
  }

  // --- Чтение состояния ------------------------------------------------------

  async refreshStatus(): Promise<void> {
    const tasks: Promise<void>[] = [
      this.httpJson<DeviceStatus>('/api/status')
        .then((status) => this.deviceStatus.set(status))
        .catch(() => undefined),
    ];

    if (this.httpApi) {
      tasks.push(
        this.httpJson<SysStatus>('/status')
          .then((status) => this.sysStatus.set(status))
          .catch(() => undefined),
      );
    }

    await Promise.all(tasks);
  }

  async loadConfig(): Promise<DeviceSettings | null> {
    if (!this.httpApi) {
      return this.settings();
    }

    try {
      const config = await this.httpJson<Record<string, unknown>>('/device/config');
      return {
        maxRpm: Number(config['maxRpm'] ?? 0),
        maxRpmAngleKicker: Number(config['maxRpmAngleKicker'] ?? 0),
        initAngle: Number(config['initAngle'] ?? 0),
        initFlashTime: Number(config['initFlashTime'] ?? 0),
        filter: Number(config['filter'] ?? 0),
        angleAdvance: !!config['angleAdvance'],
        quickShifter: !!config['quickShifterEnabled'],
        cartsSwitching: !!config['cartsSwitching'],
        highOutput: !!config['highOutput'],
        invertSensor: !!config['invertSensor'],
        invertThrottle: !!config['invertThrottle'],
        servo: !!config['servoEnabled'],
        servoType: Number(config['servoType'] ?? 0),
        servoRpm: Number(config['servoRpm'] ?? 0),
        servoPercent: Number(config['servoPercent'] ?? 0),
        type: Number(config['type'] ?? 0),
      };
    } catch {
      return this.settings();
    }
  }

  // --- Команды блока ---------------------------------------------------------

  setCart(cart: number): void {
    if (!this.send({ event: 'cart', data: { cart } })) {
      this.toast('Нет связи с блоком', 'error');
    }
  }

  async readMap(cart: number, count: number, onProgress?: (done: number, total: number) => void): Promise<MapPoint[]> {
    const points: MapPoint[] = [];

    for (let index = 0; index < count; index++) {
      const point = await this.request<MapPoint>(
        { event: 'map_point', data: { cart, point: index } },
        `map_point:${cart}:${index}`,
        2500,
      );
      points.push(point);
      onProgress?.(index + 1, count);
    }

    return points;
  }

  async savePoints(cart: number, points: MapPoint[]): Promise<void> {
    for (const point of points) {
      await this.request(
        {
          event: 'map_set',
          data: {
            cart,
            point: point.point,
            rpm: point.rpm,
            angle: point.angle,
            aa: point.aa,
            qa: point.qa,
            servo: point.servo,
          },
        },
        `map_set:${cart}:${point.point}`,
        3000,
      );
    }
  }

  async saveSettings(patch: Record<string, unknown>): Promise<void> {
    await this.request({ event: 'settings', data: patch }, 'settings', 5000);
  }

  async setAccessPoint(ssid: string, pass: string): Promise<void> {
    await this.request({ event: 'ap', data: { ssid, pass } }, 'ap', 4000);
  }

  async saveWifi(patch: { ssid?: string; pass?: string; enabled?: boolean }): Promise<void> {
    const data: Record<string, unknown> = {};

    if (patch.ssid) data['ssid'] = patch.ssid;
    if (patch.pass) data['pass'] = patch.pass;
    if (patch.enabled !== undefined) data['enabled'] = patch.enabled ? 1 : 0;

    if (this.online()) {
      await this.request({ event: 'wifi', data }, 'wifi', 6000);
      return;
    }

    if (!this.httpApi) {
      throw new Error('Нет связи с блоком');
    }

    const params = new URLSearchParams();
    if (patch.ssid) params.set('ssid', patch.ssid);
    if (patch.pass) params.set('pass', patch.pass);
    if (patch.enabled !== undefined) params.set('enabled', patch.enabled ? '1' : '0');
    await this.httpJson(`/wifi/auto?${params.toString()}`);
  }

  async connectWifi(ssid: string, pass: string): Promise<string> {
    if (this.httpApi) {
      const params = new URLSearchParams({ ssid, pass });
      const result = await this.httpJson<{ status?: string; connected?: boolean }>(`/wifi/connect?${params.toString()}`);
      return result.connected ? `Подключено к ${ssid}` : `Не удалось подключиться: ${result.status ?? 'нет ответа'}`;
    }

    await this.saveWifi({ ssid, pass });
    return 'Настройки сохранены. Блок подключается к сети в фоне.';
  }

  async scanWifi(): Promise<void> {
    this.scanning.set(true);

    try {
      if (this.online()) {
        const networks = this.waitFor<unknown>('wifi_networks', 15000, 'Блок не прислал список сетей');
        if (!this.send({ event: 'wifi_scan' })) {
          throw new Error('Нет связи с блоком');
        }
        this.wifiNetworks.set(normalizeNetworks(await networks));
        return;
      }

      if (!this.httpApi) {
        throw new Error('Сканирование доступно только по связи с блоком');
      }

      const result = await this.httpJson<{ networks?: unknown }>(`/wifi/scan?_=${Date.now()}`);
      this.wifiNetworks.set(normalizeNetworks(result.networks));
    } finally {
      this.scanning.set(false);
    }
  }

  async checkOta(): Promise<OtaCheck> {
    if (!this.httpApi) {
      throw new Error('Проверка обновлений доступна на странице http://192.168.4.1/update');
    }

    if (!this.wifiState()?.connected) {
      throw new Error('Блок не подключён к домашней сети');
    }

    const result = await this.httpJson<OtaCheck>(`/ota/check?_=${Date.now()}`);
    this.otaCheck.set(result);
    return result;
  }

  async startOta(): Promise<void> {
    if (!this.httpApi) {
      throw new Error('Обновление доступно на странице http://192.168.4.1/update');
    }

    if (!this.wifiState()?.connected) {
      throw new Error('Блок не подключён к домашней сети');
    }

    await this.httpText('/ota/update', { method: 'POST' });
  }

  async applyCartType(type: number, preset: number): Promise<void> {
    if (!this.httpApi) {
      throw new Error('Пересоздание карт доступно на странице http://192.168.4.1/');
    }

    await this.httpText(`/type?set=${type}&new_chart_type=${preset}`);
  }

  async upload(target: 'app' | 'fs', file: File, onProgress: (ratio: number) => void): Promise<UploadResult> {
    return new Promise<UploadResult>((resolve, reject) => {
      const request = new XMLHttpRequest();
      request.open('POST', `/api/update?target=${target}`);

      const key = this.deviceKey();
      if (key) {
        request.setRequestHeader('X-Device-Key', key);
      }

      request.upload.onprogress = (event) => {
        if (event.lengthComputable) {
          onProgress(event.loaded / event.total);
        }
      };

      request.onload = () => resolve({ ok: request.status >= 200 && request.status < 300, text: request.responseText });
      request.onerror = () => reject(new Error('Связь с блоком прервалась'));
      request.onabort = () => reject(new Error('Загрузка отменена'));

      request.send(file);
    });
  }

  async installCertificate(payload: string): Promise<string> {
    const headers: Record<string, string> = { 'Content-Type': 'application/json' };
    const key = this.deviceKey();
    if (key) {
      headers['X-Device-Key'] = key;
    }

    const response = await fetch('/api/tls', { method: 'POST', headers, body: payload });
    const text = await response.text();
    if (!response.ok) {
      throw new Error(text || `Ошибка ${response.status}`);
    }

    return text;
  }

  async refreshCertificate(): Promise<string> {
    if (!this.httpApi) {
      throw new Error('Получение сертификата доступно на странице http://192.168.4.1/update');
    }

    return this.httpText('/api/tls/refresh', { method: 'POST' });
  }

  async provision(key: string): Promise<string> {
    if (!this.httpApi) {
      throw new Error('Привязка доступна на странице http://192.168.4.1/update');
    }

    const text = await this.httpText('/api/provision', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ key }),
    });
    this.setDeviceKey(key);
    return text;
  }

  async reboot(): Promise<void> {
    if (this.send({ event: 'reboot' })) {
      this.toast('Блок перезагружается…', 'info');
      return;
    }

    if (!this.httpApi) {
      throw new Error('Нет связи с блоком');
    }

    await this.httpText('/core/reboot');
  }

  async resetServoError(): Promise<void> {
    await this.request({ event: 'servo_error' }, 'servo_error', 3000);
  }

  async setDiagnostics(enabled: boolean): Promise<void> {
    await this.request({ event: 'diag', data: { enabled: enabled ? 1 : 0 } }, 'diag', 4000);
  }

  startMarkScan(method: 'kick' | 'starter' | 'both'): void {
    if (!this.send({ event: 'mark_scan', data: { action: 'start', method } })) {
      this.toast('Нет связи с блоком', 'error');
    }
  }

  stopMarkScan(): void {
    this.send({ event: 'mark_scan', data: { action: 'stop' } });
  }

  applyMarkScan(): void {
    this.send({ event: 'mark_scan', data: { action: 'apply' } });
  }

  setDeviceKey(key: string): void {
    const value = key.trim();
    this.deviceKey.set(value);

    try {
      if (value) {
        localStorage.setItem(DEVICE_KEY_STORAGE, value);
      } else {
        localStorage.removeItem(DEVICE_KEY_STORAGE);
      }
    } catch {
      // Приватный режим браузера — ключ просто не переживёт перезагрузку страницы.
    }
  }

  // --- Служебное -------------------------------------------------------------

  toast(text: string, kind: ToastKind = 'info'): void {
    const id = ++this.toastId;
    this.toasts.update((list) => [...list.slice(-3), { id, text, kind }]);
    setTimeout(() => this.toasts.update((list) => list.filter((item) => item.id !== id)), 5000);
  }

  dismissToast(id: number): void {
    this.toasts.update((list) => list.filter((item) => item.id !== id));
  }

  private async httpJson<T>(url: string, init?: RequestInit): Promise<T> {
    const response = await fetch(url, { cache: 'no-store', ...init, headers: this.withKey(init?.headers) });
    if (!response.ok) {
      throw new Error(await errorText(response));
    }

    return (await response.json()) as T;
  }

  private async httpText(url: string, init?: RequestInit): Promise<string> {
    const response = await fetch(url, { cache: 'no-store', ...init, headers: this.withKey(init?.headers) });
    const text = await response.text();
    if (!response.ok) {
      throw new Error(text || `Ошибка ${response.status}`);
    }

    return text;
  }

  // Ключ устройства уходит заголовком: без него блок отказывает в опасных действиях
  // (смена настроек, обновление, перезагрузка, выдача пароля точки доступа). Пока ключ
  // не введён, запрос уходит как есть — так работает первичная настройка непривязанного блока.
  private withKey(headers?: HeadersInit): HeadersInit {
    const key = this.deviceKey().trim();
    if (!key) {
      return headers ?? {};
    }

    return { ...(headers as Record<string, string> | undefined), 'X-Device-Key': key };
  }
}

function readDeviceKey(): string {
  try {
    return localStorage.getItem(DEVICE_KEY_STORAGE) ?? '';
  } catch {
    return '';
  }
}

async function errorText(response: Response): Promise<string> {
  try {
    const text = await response.text();
    return text || `Ошибка ${response.status}`;
  } catch {
    return `Ошибка ${response.status}`;
  }
}
