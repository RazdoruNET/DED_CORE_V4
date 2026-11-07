import { ChangeDetectionStrategy, Component, computed, effect, inject, signal } from '@angular/core';
import { DeviceService } from '../core/device.service';
import { MAP_GROUPS, MapPoint, activeCart, cartPoints, pointsLabel } from '../core/models';
import { ChartComponent, ChartSeries } from '../ui/chart.component';

type MapField = 'rpm' | 'angle' | 'aa' | 'qa' | 'servo';

@Component({
  selector: 'ded-map',
  standalone: true,
  imports: [ChartComponent],
  changeDetection: ChangeDetectionStrategy.OnPush,
  templateUrl: './map.component.html',
  styleUrl: './map.component.css',
})
export class MapComponent {
  readonly device = inject(DeviceService);

  readonly cart = signal(1);
  readonly points = signal<MapPoint[]>([]);
  readonly original = signal<MapPoint[]>([]);
  readonly loading = signal(false);
  readonly saving = signal(false);
  readonly error = signal('');
  readonly progress = signal(0);
  readonly mode = signal<'angle' | 'servo'>('angle');
  readonly groups = MAP_GROUPS;

  private started = false;
  private loadTicket = 0;

  readonly count = computed(() => cartPoints(this.device.telemetry()?.type ?? 0));

  readonly dirty = computed(() => {
    const original = this.original();
    return this.points().filter((point, index) => !samePoint(point, original[index]));
  });

  readonly activeIndex = computed(() => {
    const rpm = this.device.telemetry()?.rpm ?? 0;
    const points = this.points();
    let best = -1;
    let distance = Number.POSITIVE_INFINITY;

    points.forEach((point, index) => {
      const delta = Math.abs(point.rpm - rpm);
      if (delta < distance) {
        distance = delta;
        best = index;
      }
    });

    return points.length > 0 ? best : -1;
  });

  readonly chartSeries = computed<ChartSeries[]>(() => {
    const points = this.points();

    if (this.mode() === 'servo') {
      return [
        {
          label: 'Открытие серво, %',
          color: '#35c27a',
          points: points.map((point) => ({ x: point.rpm, y: point.servo })),
        },
      ];
    }

    return [
      {
        label: 'Угол опережения',
        color: '#d21f3c',
        points: points.map((point) => ({ x: point.rpm, y: point.angle })),
      },
      {
        label: 'Коррекция по дросселю',
        color: '#e8b339',
        points: points.map((point) => ({ x: point.rpm, y: point.aa })),
      },
    ];
  });

  readonly markerRpm = computed(() => this.device.telemetry()?.rpm ?? null);

  readonly groupRanges = computed(() =>
    this.groups.map((group) => ({
      ...group,
      to: Math.min(group.to, this.count() - 1),
    })),
  );

  constructor() {
    effect(() => {
      const telemetry = this.device.telemetry();
      if (!telemetry || this.started) {
        return;
      }

      this.started = true;
      this.cart.set(activeCart(telemetry));
      void this.load();
    });
  }

  selectCart(cart: number): void {
    if (cart === this.cart()) {
      return;
    }

    this.cart.set(cart);
    this.device.setCart(cart);
    void this.load();
  }

  async load(): Promise<void> {
    if (!this.device.online()) {
      this.error.set('Нет связи с блоком: карту можно читать и править только по WebSocket.');
      return;
    }

    const ticket = ++this.loadTicket;
    const cart = this.cart();
    const count = this.count();

    this.loading.set(true);
    this.error.set('');
    this.progress.set(0);

    try {
      const points = await this.device.readMap(cart, count, (done, total) => {
        if (ticket === this.loadTicket) {
          this.progress.set(Math.round((done / total) * 100));
        }
      });

      if (ticket !== this.loadTicket) {
        return;
      }

      this.points.set(points.map((point) => ({ ...point })));
      this.original.set(points.map((point) => ({ ...point })));
    } catch (error) {
      if (ticket === this.loadTicket) {
        this.error.set(errorMessage(error));
      }
    } finally {
      if (ticket === this.loadTicket) {
        this.loading.set(false);
      }
    }
  }

  edit(index: number, field: MapField, event: Event): void {
    const input = event.target as HTMLInputElement;
    const value = Number(input.value);

    if (!Number.isFinite(value)) {
      return;
    }

    this.points.update((points) =>
      points.map((point, pointIndex) => (pointIndex === index ? { ...point, [field]: value } : point)),
    );
  }

  adjustGroup(from: number, to: number, delta: number): void {
    this.points.update((points) =>
      points.map((point, index) => {
        if (index < from || index > to) {
          return point;
        }

        return { ...point, angle: clampAngle(round1(point.angle + delta)) };
      }),
    );
  }

  revert(): void {
    this.points.set(this.original().map((point) => ({ ...point })));
  }

  async save(): Promise<void> {
    const changed = this.dirty();
    if (changed.length === 0) {
      this.device.toast('Нет изменений для записи', 'info');
      return;
    }

    this.saving.set(true);
    this.error.set('');

    try {
      await this.device.savePoints(this.cart(), changed);
      this.original.set(this.points().map((point) => ({ ...point })));
      this.device.toast(`Записано точек: ${changed.length}`, 'ok');
    } catch (error) {
      this.error.set(errorMessage(error));
      this.device.toast('Не удалось записать карту', 'error');
    } finally {
      this.saving.set(false);
    }
  }

  pointsLabel(count: number): string {
    return pointsLabel(count);
  }

  dirtyAt(index: number): boolean {
    return !samePoint(this.points()[index], this.original()[index]);
  }

  setMode(mode: 'angle' | 'servo'): void {
    this.mode.set(mode);
  }
}

function samePoint(left: MapPoint | undefined, right: MapPoint | undefined): boolean {
  if (!left || !right) {
    return left === right;
  }

  return (
    left.rpm === right.rpm &&
    left.angle === right.angle &&
    left.aa === right.aa &&
    left.qa === right.qa &&
    left.servo === right.servo
  );
}

function round1(value: number): number {
  return Math.round(value * 10) / 10;
}

function clampAngle(value: number): number {
  return Math.max(0, Math.min(60, value));
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
