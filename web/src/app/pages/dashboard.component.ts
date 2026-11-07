import { ChangeDetectionStrategy, Component, computed, inject } from '@angular/core';
import { DeviceService } from '../core/device.service';
import { activeCart, cartPoints, pointsLabel, wifiStatusLabel } from '../core/models';
import { ChartComponent, ChartSeries } from '../ui/chart.component';

@Component({
  selector: 'ded-dashboard',
  standalone: true,
  imports: [ChartComponent],
  changeDetection: ChangeDetectionStrategy.OnPush,
  templateUrl: './dashboard.component.html',
  styleUrl: './dashboard.component.css',
})
export class DashboardComponent {
  readonly device = inject(DeviceService);

  readonly rpm = computed(() => Math.round(this.device.telemetry()?.rpm ?? 0));
  readonly throttle = computed(() => Math.max(0, Math.min(100, Math.round(this.device.telemetry()?.throttle ?? 0))));
  readonly engineTemp = computed(() => Math.round(this.device.telemetry()?.termo ?? 0));
  readonly cpuTemp = computed(() => Math.round((this.device.telemetry()?.cpu_termo ?? 0) * 10) / 10);
  readonly maxRpm = computed(() => {
    const configured = this.device.telemetry()?.max_rpm ?? 0;
    return configured > 0 ? configured : 12000;
  });
  readonly rpmPercent = computed(() => Math.min(100, Math.round((this.rpm() / this.maxRpm()) * 100)));
  readonly gaugeDash = computed(() => {
    const length = Math.PI * 80;
    return `${((this.rpmPercent() / 100) * length).toFixed(1)} ${length.toFixed(1)}`;
  });
  readonly cart = computed(() => {
    const telemetry = this.device.telemetry();
    return telemetry ? activeCart(telemetry) : 1;
  });
  readonly points = computed(() => cartPoints(this.device.telemetry()?.type ?? 0));
  readonly motorRunning = computed(() => this.rpm() > 0);

  readonly rpmSeries = computed<ChartSeries[]>(() => [
    {
      label: 'Обороты',
      color: '#d21f3c',
      points: this.device.history().map((sample, index) => ({ x: index, y: sample.rpm })),
    },
  ]);

  readonly throttleSeries = computed<ChartSeries[]>(() => [
    {
      label: 'Дроссель, %',
      color: '#35c27a',
      points: this.device.history().map((sample, index) => ({ x: index, y: sample.throttle })),
    },
  ]);

  readonly wifi = computed(() => this.device.wifiState());

  readonly apName = computed(() => this.device.telemetry()?.wsid || '—');

  readonly otaLabel = computed(() => {
    const info = this.device.info();
    const telemetry = this.device.telemetry();

    if (info?.ota_update_available || telemetry?.ota_update_available) {
      return `доступно ${info?.ota_last_version || telemetry?.ota_last_version || ''}`.trim();
    }

    if (!this.wifi()?.connected) {
      return 'нужна домашняя сеть';
    }

    return 'обновлений нет';
  });

  readonly otaAvailable = computed(
    () => !!(this.device.info()?.ota_update_available || this.device.telemetry()?.ota_update_available),
  );

  readonly motorLabel = computed(() => (this.motorRunning() ? 'работает' : 'стоит'));

  readonly typeLabel = computed(() => pointsLabel(this.points()));

  readonly wifiStatusLabel = computed(() => {
    const wifi = this.wifi();
    if (!wifi) return 'нет данных';
    if (wifi.connected) return wifi.ssid || 'подключено';
    return wifi.ssid ? `${wifi.ssid}: ${wifiStatusLabel(wifi.status)}` : 'не настроена';
  });
}
