import { ChangeDetectionStrategy, Component, computed, inject, signal } from '@angular/core';
import { DeviceService } from '../core/device.service';

@Component({
  selector: 'ded-diagnostics',
  standalone: true,
  changeDetection: ChangeDetectionStrategy.OnPush,
  templateUrl: './diagnostics.component.html',
  styleUrl: './diagnostics.component.css',
})
export class DiagnosticsComponent {
  readonly device = inject(DeviceService);

  readonly rebootBusy = signal(false);
  readonly diagBusy = signal(false);
  readonly error = signal('');

  readonly uptime = computed(() => formatDuration((this.device.telemetry()?.micros ?? 0) / 1_000_000));
  readonly telemetryJson = computed(() => JSON.stringify(this.device.telemetry() ?? {}, null, 2));
  readonly statusJson = computed(() => JSON.stringify(this.device.deviceStatus() ?? {}, null, 2));
  readonly sysStatusJson = computed(() => JSON.stringify(this.device.sysStatus() ?? {}, null, 2));
  readonly diagEnabled = computed(() => !!this.device.info()?.diag_wifi_enabled);

  readonly wsLabel = computed(() => {
    if (this.device.online()) return 'подключён';
    if (this.device.connection() === 'connecting') return 'подключается';
    return 'нет связи';
  });

  readonly hostLabel = computed(() => location.host);

  readonly protocolLabel = computed(() => (this.device.secure ? 'https + wss' : 'http + ws'));

  async refresh(): Promise<void> {
    await this.device.refreshStatus();
  }

  async reboot(): Promise<void> {
    if (!window.confirm('Перезагрузить блок? Телеметрия и связь прервутся на несколько секунд.')) {
      return;
    }

    this.rebootBusy.set(true);
    this.error.set('');

    try {
      await this.device.reboot();
    } catch (error) {
      this.error.set(errorMessage(error));
    } finally {
      this.rebootBusy.set(false);
    }
  }

  async toggleDiagnostics(): Promise<void> {
    const enabled = !this.diagEnabled();

    const confirmed = window.confirm(
      enabled
        ? 'Включить режим удалённой диагностики?\n\n' +
          'Блок перезагрузится и на время работы режима (до 30 минут) не будет поднимать точку доступа, ' +
          'веб-страницу и Bluetooth. Управлять блоком в это время нельзя.'
        : 'Выключить режим диагностики?\nБлок перезагрузится и вернётся к обычной работе.',
    );

    if (!confirmed) {
      return;
    }

    this.diagBusy.set(true);
    this.error.set('');

    try {
      await this.device.setDiagnostics(enabled);
      this.device.toast('Блок перезагружается', 'info');
    } catch (error) {
      this.error.set(errorMessage(error));
    } finally {
      this.diagBusy.set(false);
    }
  }
}

function formatDuration(seconds: number): string {
  const total = Math.max(0, Math.floor(seconds));
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  const rest = total % 60;

  if (hours > 0) {
    return `${hours} ч ${minutes} мин`;
  }
  if (minutes > 0) {
    return `${minutes} мин ${rest} с`;
  }
  return `${rest} с`;
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
