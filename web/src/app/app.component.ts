import { ChangeDetectionStrategy, Component, computed, inject, signal } from '@angular/core';
import { DeviceService } from './core/device.service';
import { Tab } from './core/models';
import { AssistantComponent } from './pages/assistant.component';
import { DashboardComponent } from './pages/dashboard.component';
import { DiagnosticsComponent } from './pages/diagnostics.component';
import { MapComponent } from './pages/map.component';
import { NetworkComponent } from './pages/network.component';
import { SettingsComponent } from './pages/settings.component';

@Component({
  selector: 'ded-root',
  standalone: true,
  imports: [
    DashboardComponent,
    MapComponent,
    SettingsComponent,
    NetworkComponent,
    DiagnosticsComponent,
    AssistantComponent,
  ],
  changeDetection: ChangeDetectionStrategy.OnPush,
  templateUrl: './app.component.html',
  styleUrl: './app.component.css',
})
export class AppComponent {
  readonly device = inject(DeviceService);

  readonly tab = signal<Tab>('dashboard');
  readonly host = location.host;

  readonly tabs: ReadonlyArray<{ id: Tab; label: string; short: string }> = [
    { id: 'dashboard', label: 'Дашборд', short: 'Приборы' },
    { id: 'map', label: 'Карта', short: 'Карта' },
    { id: 'settings', label: 'Настройки', short: 'Настройки' },
    { id: 'assistant', label: 'Люси', short: 'Люси' },
    { id: 'network', label: 'Сеть и обновления', short: 'Сеть' },
    { id: 'diagnostics', label: 'Диагностика', short: 'Диаг' },
  ];

  readonly connectionLabel = computed(() => {
    switch (this.device.connection()) {
      case 'online':
        return 'связь есть';
      case 'connecting':
        return 'подключаюсь…';
      default:
        return 'нет связи';
    }
  });

  readonly version = this.device.firmwareVersion;

  constructor() {
    this.device.connect();
    void this.device.refreshStatus();
  }

  reconnect(): void {
    if (!this.device.online()) {
      this.device.toast('Переподключаюсь к блоку', 'info');
      this.device.connect();
    }
  }

  dismiss(id: number): void {
    this.device.dismissToast(id);
  }
}
