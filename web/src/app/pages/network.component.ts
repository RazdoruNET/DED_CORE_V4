import { ChangeDetectionStrategy, Component, computed, effect, inject, signal } from '@angular/core';
import { DeviceService } from '../core/device.service';
import { signalBars, wifiStatusLabel } from '../core/models';

@Component({
  selector: 'ded-network',
  standalone: true,
  changeDetection: ChangeDetectionStrategy.OnPush,
  templateUrl: './network.component.html',
  styleUrl: './network.component.css',
})
export class NetworkComponent {
  readonly device = inject(DeviceService);

  readonly apSsid = signal('');
  readonly apPass = signal('');
  readonly homeSsid = signal('');
  readonly homePass = signal('');
  readonly busy = signal<string | null>(null);
  readonly error = signal('');
  readonly uploadProgress = signal(0);
  readonly uploading = signal(false);
  readonly certMode = signal<'json' | 'pem'>('json');
  readonly certFile = signal<File | null>(null);
  readonly keyFile = signal<File | null>(null);
  readonly deviceKeyInput = signal(this.device.deviceKey());

  readonly otaCheck = this.device.otaCheck;
  readonly scanning = this.device.scanning;
  readonly wifi = this.device.wifiState;
  readonly networks = this.device.wifiNetworks;

  readonly otaLabel = computed(() => {
    const check = this.otaCheck();
    const info = this.device.info();
    if (check?.updateAvailable) {
      return `доступна ${check.newVersion ?? ''}`.trim();
    }
    if (info?.ota_update_available) {
      return `доступна ${info.ota_last_version}`;
    }
    return 'обновлений нет';
  });

  readonly otaAvailable = computed(() => !!this.otaCheck()?.updateAvailable || !!this.device.info()?.ota_update_available);
  readonly otaReady = computed(() => !!this.wifi()?.connected);
  readonly canCheckOta = computed(() => this.device.httpApi && this.otaReady());
  readonly canInstallOta = computed(() => this.canCheckOta() && this.otaAvailable());

  readonly lastCheckLabel = computed(() => {
    const wifi = this.wifi();
    if (!wifi || wifi.lastCheck === 0) {
      return 'ещё не проверялось';
    }

    const nowMs = (this.device.telemetry()?.micros ?? 0) / 1000;
    const ageMinutes = Math.max(0, Math.round((nowMs - wifi.lastCheck) / 60000));
    return ageMinutes < 1 ? 'только что' : `${ageMinutes} мин назад`;
  });

  readonly certLabel = computed(() => {
    const status = this.device.deviceStatus();
    if (!status) return 'нет данных';
    if (!status.cert) return 'не установлен';
    if (status.certDaysLeft === -1) return 'установлен, срок неизвестен';
    if (status.certDaysLeft === -2) return 'установлен, часы не выставлены';
    if ((status.certDaysLeft ?? 0) < 0) return 'истёк';
    return `установлен, осталось ${status.certDaysLeft} дн.`;
  });

  constructor() {
    effect(() => {
      const wifi = this.wifi();
      if (wifi && this.homeSsid() === '') {
        this.homeSsid.set(wifi.ssid);
      }

      const ap = this.device.telemetry()?.wsid;
      if (ap && this.apSsid() === '') {
        this.apSsid.set(ap);
      }
    });
  }

  wifiStatusLabel(status: string): string {
    return wifiStatusLabel(status);
  }

  bars(rssi: number): number {
    return signalBars(rssi);
  }

  sizeLabel(): string {
    const size = this.otaCheck()?.size;
    return size ? `${Math.round(size / 1024)} КБ` : '—';
  }

  async refresh(): Promise<void> {
    await this.device.refreshStatus();
  }

  async saveAccessPoint(): Promise<void> {
    const ssid = this.apSsid().trim();
    if (!ssid) {
      this.device.toast('Имя точки доступа не может быть пустым', 'error');
      return;
    }

    const confirmed = window.confirm(
      `Переименовать точку доступа в «${ssid}»?\nПосле сохранения телефон потеряет связь и её нужно будет выбрать заново.`,
    );
    if (!confirmed) {
      return;
    }

    await this.run('ap', async () => {
      await this.device.setAccessPoint(ssid, this.apPass().trim());
      this.apPass.set('');
      this.device.toast('Точка доступа сохранена', 'ok');
    });
  }

  async scan(): Promise<void> {
    await this.run('scan', async () => {
      await this.device.scanWifi();
    });
  }

  useNetwork(ssid: string): void {
    this.homeSsid.set(ssid);
    this.homePass.set('');
  }

  async connectHome(): Promise<void> {
    const ssid = this.homeSsid().trim();
    if (!ssid) {
      this.device.toast('Введите имя домашней сети', 'error');
      return;
    }

    await this.run('connect', async () => {
      const result = await this.device.connectWifi(ssid, this.homePass());
      this.homePass.set('');
      this.device.toast(result, 'ok');
      await this.device.refreshStatus();
    });
  }

  async toggleAutoUpdate(): Promise<void> {
    const enabled = !(this.wifi()?.auto ?? false);
    await this.run('auto', async () => {
      await this.device.saveWifi({ enabled });
      this.device.toast(`Автообновление ${enabled ? 'включено' : 'выключено'}`, 'ok');
    });
  }

  async checkOta(): Promise<void> {
    if (!this.otaReady()) {
      this.device.toast('Сначала подключите блок к домашней сети', 'error');
      return;
    }

    await this.run('check', async () => {
      const result = await this.device.checkOta();
      this.device.toast(
        result.updateAvailable ? `Доступна версия ${result.newVersion ?? ''}` : 'Обновлений нет',
        result.updateAvailable ? 'info' : 'ok',
      );
    });
  }

  async runOta(): Promise<void> {
    if (!this.otaReady()) {
      this.device.toast('Сначала подключите блок к домашней сети', 'error');
      return;
    }

    if (!this.otaAvailable()) {
      this.device.toast('Сначала проверьте обновление', 'error');
      return;
    }

    if (!window.confirm('Скачать и установить обновление? Блок перезагрузится, связь прервётся.')) {
      return;
    }

    this.busy.set('ota');
    this.error.set('');

    try {
      await this.device.startOta();
      this.device.toast('Обновление запущено, блок перезагрузится', 'ok');
    } catch {
      this.device.toast('Блок перезагружается для установки обновления', 'info');
    } finally {
      this.busy.set(null);
    }
  }

  async upload(target: 'app' | 'fs', event: Event): Promise<void> {
    const input = event.target as HTMLInputElement;
    const file = input.files?.[0];
    input.value = '';

    if (!file) {
      return;
    }

    const label = target === 'app' ? 'прошивку' : 'файловую систему';
    if (!window.confirm(`Записать ${label} (${Math.round(file.size / 1024)} КБ)? Не выключайте питание.`)) {
      return;
    }

    this.uploading.set(true);
    this.uploadProgress.set(0);
    this.error.set('');

    try {
      const result = await this.device.upload(target, file, (ratio) => this.uploadProgress.set(Math.round(ratio * 100)));
      if (result.ok) {
        this.device.toast('Файл принят блоком', 'ok');
      } else {
        this.error.set(result.text || 'Блок отклонил файл');
      }
    } catch (error) {
      const message = errorMessage(error);
      if (/перезагруз|reboot|reset|network|load failed/i.test(message)) {
        this.device.toast('Блок перезагружается после записи', 'info');
      } else {
        this.error.set(message);
      }
    } finally {
      this.uploading.set(false);
    }
  }

  onCertFile(event: Event): void {
    const input = event.target as HTMLInputElement;
    this.certFile.set(input.files?.[0] ?? null);
  }

  onKeyFile(event: Event): void {
    const input = event.target as HTMLInputElement;
    this.keyFile.set(input.files?.[0] ?? null);
  }

  async installCertificate(): Promise<void> {
    const cert = this.certFile();
    const key = this.keyFile();

    if (!cert) {
      this.device.toast('Выберите файл сертификата', 'error');
      return;
    }

    await this.run('cert', async () => {
      let payload: string;

      if (this.certMode() === 'json' || cert.name.toLowerCase().endsWith('.json')) {
        payload = await cert.text();
      } else {
        if (!key) {
          throw new Error('Для cert.pem нужен ещё и key.pem');
        }
        payload = JSON.stringify({ cert: await cert.text(), key: await key.text() });
      }

      await this.device.installCertificate(payload);
      this.device.toast('Сертификат установлен, HTTPS перезапускается', 'ok');
      this.certFile.set(null);
      this.keyFile.set(null);
      await this.device.refreshStatus();
    });
  }

  async refreshCertificate(): Promise<void> {
    await this.run('cert', async () => {
      await this.device.refreshCertificate();
      this.device.toast('Сертификат запрошен у сервиса', 'ok');
      await this.device.refreshStatus();
    });
  }

  async provision(): Promise<void> {
    const key = this.deviceKeyInput().trim();

    if (key.length < 32) {
      this.device.toast('Ключ устройства слишком короткий', 'error');
      return;
    }

    await this.run('pair', async () => {
      await this.device.provision(key);
      this.device.toast('Ключ устройства сохранён', 'ok');
      await this.device.refreshStatus();
    });
  }

  saveDeviceKey(): void {
    this.device.setDeviceKey(this.deviceKeyInput());
    this.device.toast('Ключ сохранён в браузере (для запросов по HTTPS)', 'ok');
  }

  private async run(id: string, action: () => Promise<void>): Promise<void> {
    this.busy.set(id);
    this.error.set('');

    try {
      await action();
    } catch (error) {
      this.error.set(errorMessage(error));
    } finally {
      this.busy.set(null);
    }
  }
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
