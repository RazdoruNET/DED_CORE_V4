import { ChangeDetectionStrategy, Component, computed, effect, inject, signal } from '@angular/core';
import { DeviceService } from '../core/device.service';
import { CART_PRESETS, DeviceSettings, cartPoints, pointsLabel } from '../core/models';

type BoolSetting =
  | 'angleAdvance'
  | 'quickShifter'
  | 'cartsSwitching'
  | 'highOutput'
  | 'invertSensor'
  | 'invertThrottle'
  | 'servo';

type NumberSetting =
  | 'maxRpm'
  | 'maxRpmAngleKicker'
  | 'initAngle'
  | 'initFlashTime'
  | 'filter'
  | 'servoType'
  | 'servoRpm'
  | 'servoPercent';

@Component({
  selector: 'ded-settings',
  standalone: true,
  changeDetection: ChangeDetectionStrategy.OnPush,
  templateUrl: './settings.component.html',
  styleUrl: './settings.component.css',
})
export class SettingsComponent {
  readonly device = inject(DeviceService);
  readonly presets = CART_PRESETS;

  readonly draft = signal<DeviceSettings | null>(null);
  readonly saving = signal(false);
  readonly error = signal('');
  readonly selectedType = signal(0);
  readonly selectedPreset = signal(0);
  readonly markMethod = signal<'kick' | 'starter' | 'both'>('kick');
  readonly markScan = this.device.markScan;
  readonly pairing = signal(false);
  readonly pairNote = signal('');

  readonly patch = computed<Record<string, unknown>>(() => {
    const draft = this.draft();
    const current = this.device.settings();
    if (!draft || !current) {
      return {};
    }

    const patch: Record<string, unknown> = {};
    if (draft.maxRpm !== current.maxRpm) patch['max_rpm'] = draft.maxRpm;
    if (draft.maxRpmAngleKicker !== current.maxRpmAngleKicker) patch['max_rpm_angle_kiker'] = draft.maxRpmAngleKicker;
    if (draft.initAngle !== current.initAngle) patch['init_angle'] = draft.initAngle;
    if (draft.initFlashTime !== current.initFlashTime) patch['init_flash_time'] = draft.initFlashTime;
    if (draft.filter !== current.filter) patch['filter_val'] = draft.filter;
    if (draft.angleAdvance !== current.angleAdvance) patch['aa'] = draft.angleAdvance ? 1 : 0;
    if (draft.quickShifter !== current.quickShifter) patch['qs'] = draft.quickShifter ? 1 : 0;
    if (draft.cartsSwitching !== current.cartsSwitching) patch['cc'] = draft.cartsSwitching ? 1 : 0;
    if (draft.highOutput !== current.highOutput) patch['high'] = draft.highOutput ? 1 : 0;
    if (draft.invertSensor !== current.invertSensor) patch['invert_sensor'] = draft.invertSensor ? 1 : 0;
    if (draft.invertThrottle !== current.invertThrottle) patch['invert_throttle'] = draft.invertThrottle ? 1 : 0;
    if (draft.servo !== current.servo) patch['servo'] = draft.servo ? 1 : 0;
    if (draft.servoType !== current.servoType) patch['servo_type'] = draft.servoType;
    if (draft.servoRpm !== current.servoRpm) patch['servo_rpm'] = draft.servoRpm;
    if (draft.servoPercent !== current.servoPercent) patch['servo_percent'] = draft.servoPercent;

    return patch;
  });

  readonly dirtyCount = computed(() => Object.keys(this.patch()).length);

  readonly markBusy = computed(() => {
    const state = this.markScan()?.state;
    return state === 'scanning' || state === 'refining';
  });

  readonly markDone = computed(() => this.markScan()?.state === 'done' || (this.markScan()?.best ?? -1) >= 0);

  readonly markStatus = computed(() => {
    const scan = this.markScan();
    if (!scan) return 'не запускалось';
    switch (scan.state) {
      case 'scanning':
        return `перебор: попытка ${scan.attempt} из ${scan.total}`;
      case 'refining':
        return 'уточняю найденный угол';
      case 'done':
        return `готово, найдено ${scan.best}°`;
      case 'cancelled':
        return scan.note || 'остановлено';
      default:
        return 'не запускалось';
    }
  });

  readonly currentPoints = computed(() => cartPoints(this.device.telemetry()?.type ?? 0));

  constructor() {
    effect(() => {
      const current = this.device.settings();
      if (!current) {
        return;
      }

      const draft = this.draft();
      if (draft === null) {
        this.draft.set({ ...current });
        this.selectedType.set(current.type);
        return;
      }

      // Если владелец ничего не менял, форма повторяет блок (в том числе после сохранения).
      if (Object.keys(this.patch()).length === 0 && !sameSettings(draft, current)) {
        this.draft.set({ ...current });
      }
    });
  }

  pointsLabel(count: number): string {
    return pointsLabel(count);
  }

  setNumber(field: NumberSetting, event: Event, min: number, max: number): void {
    const value = Number((event.target as HTMLInputElement).value);
    if (!Number.isFinite(value)) {
      return;
    }

    const clamped = Math.max(min, Math.min(max, Math.round(value)));
    this.draft.update((draft) => (draft ? ({ ...draft, [field]: clamped } as DeviceSettings) : draft));
  }

  toggle(field: BoolSetting): void {
    this.draft.update((draft) => (draft ? ({ ...draft, [field]: !draft[field] } as DeviceSettings) : draft));
  }

  reset(): void {
    const current = this.device.settings();
    if (current) {
      this.draft.set({ ...current });
      this.error.set('');
    }
  }

  async save(): Promise<void> {
    const patch = this.patch();
    if (Object.keys(patch).length === 0) {
      this.device.toast('Нет изменений для записи', 'info');
      return;
    }

    this.saving.set(true);
    this.error.set('');

    try {
      await this.device.saveSettings(patch);
      await delay(350);
      const current = this.device.settings();
      if (current) {
        this.draft.set({ ...current });
      }
      this.device.toast('Настройки записаны', 'ok');
    } catch (error) {
      this.error.set(errorMessage(error));
      this.device.toast('Не удалось записать настройки', 'error');
    } finally {
      this.saving.set(false);
    }
  }

  async applyCartType(): Promise<void> {
    const points = cartPoints(this.selectedType());
    const preset = this.presets.find((item) => item.id === this.selectedPreset());

    const confirmed = window.confirm(
      `Пересоздать карты по шаблону «${preset?.label ?? '—'}»?\n` +
        `Все три карты получат ${points} точек, текущие углы будут заменены.`,
    );

    if (!confirmed) {
      return;
    }

    this.saving.set(true);
    this.error.set('');

    try {
      await this.device.applyCartType(this.selectedType(), this.selectedPreset());
      this.device.toast('Карты пересозданы. Вкладка «Карта» перечитает точки.', 'ok');
    } catch (error) {
      this.error.set(errorMessage(error));
    } finally {
      this.saving.set(false);
    }
  }

  startMarkScan(): void {
    this.device.startMarkScan(this.markMethod());
  }

  stopMarkScan(): void {
    this.device.stopMarkScan();
  }

  applyMarkScan(): void {
    this.device.applyMarkScan();
    this.device.toast('Найденный угол метки применён', 'ok');
  }

  async startModulePairing(): Promise<void> {
    if (this.pairing()) {
      return;
    }

    this.pairing.set(true);
    this.pairNote.set('Открываю окно сопряжения…');

    try {
      const result = await this.device.startModulePairing();
      const seconds = Math.round(result.windowMs / 1000);

      this.pairNote.set(`Сеть ${result.ssid} открыта на ${seconds} с.`);
      this.device.toast('Окно сопряжения открыто, включите модуль', 'ok');
    } catch (error) {
      this.pairNote.set(errorMessage(error));
      this.device.toast(errorMessage(error), 'error');
    } finally {
      this.pairing.set(false);
    }
  }

  async stopModulePairing(): Promise<void> {
    if (this.pairing()) {
      return;
    }

    this.pairing.set(true);

    try {
      await this.device.stopModulePairing();
      this.pairNote.set('Окно сопряжения закрыто');
    } catch (error) {
      this.pairNote.set(errorMessage(error));
    } finally {
      this.pairing.set(false);
    }
  }

  async resetServoError(): Promise<void> {
    try {
      await this.device.resetServoError();
      this.device.toast('Ошибка сервопривода сброшена', 'ok');
    } catch (error) {
      this.device.toast(errorMessage(error), 'error');
    }
  }

  refreshFromDevice(): void {
    this.device.toast('Запрашиваю настройки у блока', 'info');
    void this.device.saveSettings({}).catch((error: unknown) => this.device.toast(errorMessage(error), 'error'));
  }
}

function sameSettings(left: DeviceSettings, right: DeviceSettings): boolean {
  return (
    left.maxRpm === right.maxRpm &&
    left.maxRpmAngleKicker === right.maxRpmAngleKicker &&
    left.initAngle === right.initAngle &&
    left.initFlashTime === right.initFlashTime &&
    left.filter === right.filter &&
    left.angleAdvance === right.angleAdvance &&
    left.quickShifter === right.quickShifter &&
    left.cartsSwitching === right.cartsSwitching &&
    left.highOutput === right.highOutput &&
    left.invertSensor === right.invertSensor &&
    left.invertThrottle === right.invertThrottle &&
    left.servo === right.servo &&
    left.servoType === right.servoType &&
    left.servoRpm === right.servoRpm &&
    left.servoPercent === right.servoPercent &&
    left.type === right.type
  );
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
