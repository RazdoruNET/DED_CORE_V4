import { ChangeDetectionStrategy, Component, computed, input } from '@angular/core';

export interface ChartPoint {
  x: number;
  y: number;
}

export interface ChartSeries {
  label: string;
  color: string;
  points: ChartPoint[];
}

interface RenderedSeries extends ChartSeries {
  dots: ChartPoint[];
}

interface Tick {
  value: number;
  x: number;
  y: number;
  label: string;
}

const WIDTH = 640;
const HEIGHT = 260;
const PAD_LEFT = 52;
const PAD_RIGHT = 18;
const PAD_TOP = 18;
const PAD_BOTTOM = 38;

@Component({
  selector: 'ded-chart',
  standalone: true,
  changeDetection: ChangeDetectionStrategy.OnPush,
  template: `
    <div class="chart">
      <div class="legend">
        @for (item of series(); track item.label) {
          <span class="legend-item"><i [style.background]="item.color"></i>{{ item.label }}</span>
        }
      </div>

      <svg [attr.viewBox]="'0 0 ' + width + ' ' + height" role="img" [attr.aria-label]="ariaLabel()">
        @for (line of yGrid(); track $index) {
          <line class="grid-line" [attr.x1]="padLeft" [attr.x2]="width - padRight" [attr.y1]="line.y" [attr.y2]="line.y" />
          <text class="axis-label" [attr.x]="padLeft - 8" [attr.y]="line.y + 4" text-anchor="end">{{ line.label }}</text>
        }

        @for (line of xGrid(); track $index) {
          <line class="grid-line" [attr.x1]="line.x" [attr.x2]="line.x" [attr.y1]="padTop" [attr.y2]="height - padBottom" />
          <text class="axis-label" [attr.x]="line.x" [attr.y]="height - padBottom + 18" text-anchor="middle">{{ line.label }}</text>
        }

        <line class="axis" [attr.x1]="padLeft" [attr.x2]="padLeft" [attr.y1]="padTop" [attr.y2]="height - padBottom" />
        <line class="axis" [attr.x1]="padLeft" [attr.x2]="width - padRight" [attr.y1]="height - padBottom" [attr.y2]="height - padBottom" />

        @if (markerPoint(); as marker) {
          <line class="marker" [attr.x1]="marker.x" [attr.x2]="marker.x" [attr.y1]="padTop" [attr.y2]="height - padBottom" />
          <text class="marker-label" [attr.x]="marker.x + 4" [attr.y]="padTop + 12">{{ markerLabel() }}</text>
        }

        @for (item of rendered(); track item.label) {
          <polyline class="series" [attr.points]="pointsAttr(item.dots)" [attr.stroke]="item.color" fill="none" />
          @for (dot of item.dots; track $index) {
            <circle class="dot" [attr.cx]="dot.x" [attr.cy]="dot.y" r="3" [attr.fill]="item.color" />
          }
        }

        <text class="unit" [attr.x]="width - padRight" [attr.y]="height - 6" text-anchor="end">{{ xUnit() }}</text>
      </svg>
    </div>
  `,
  styles: `
    :host {
      display: block;
    }

    .chart {
      width: 100%;
    }

    .legend {
      display: flex;
      flex-wrap: wrap;
      gap: 6px 16px;
      margin-bottom: 4px;
      font-size: 12px;
      color: var(--muted);
    }

    .legend-item {
      display: inline-flex;
      align-items: center;
      gap: 6px;
    }

    .legend-item i {
      width: 10px;
      height: 10px;
      border-radius: 3px;
    }

    svg {
      display: block;
      width: 100%;
      height: auto;
      overflow: visible;
    }

    .grid-line {
      stroke: #262a31;
      stroke-width: 1;
    }

    .axis {
      stroke: #3a3f48;
      stroke-width: 1;
    }

    .axis-label,
    .unit {
      fill: #7d8590;
      font-size: 11px;
      font-family: inherit;
    }

    .series {
      stroke-width: 2.2;
      stroke-linejoin: round;
      stroke-linecap: round;
    }

    .dot {
      stroke: #0e1014;
      stroke-width: 1.5;
    }

    .marker {
      stroke: #e8b339;
      stroke-width: 1.4;
      stroke-dasharray: 5 4;
    }

    .marker-label {
      fill: #e8b339;
      font-size: 11px;
    }
  `,
})
export class ChartComponent {
  readonly series = input<ChartSeries[]>([]);
  readonly markerX = input<number | null>(null);
  readonly markerLabel = input('');
  readonly xUnit = input('об/мин');
  readonly xInteger = input(false);
  readonly ariaLabel = input('График');

  readonly width = WIDTH;
  readonly height = HEIGHT;
  readonly padLeft = PAD_LEFT;
  readonly padRight = PAD_RIGHT;
  readonly padTop = PAD_TOP;
  readonly padBottom = PAD_BOTTOM;

  private readonly bounds = computed(() => {
    const points = this.series().flatMap((item) => item.points);
    const marker = this.markerX();

    if (points.length === 0) {
      return { xMin: 0, xMax: 1, yMin: 0, yMax: 1 };
    }

    const xs = points.map((point) => point.x);
    const ys = points.map((point) => point.y);

    if (marker !== null && Number.isFinite(marker)) {
      xs.push(marker);
    }

    let xMin = Math.min(...xs);
    let xMax = Math.max(...xs);
    let yMin = Math.min(...ys);
    let yMax = Math.max(...ys);
    const rawMin = yMin;

    if (xMax - xMin < 1) {
      xMax += 1;
      xMin -= 1;
    }

    if (yMax - yMin < 1) {
      yMax += 1;
      yMin -= 1;
    }

    const yPad = (yMax - yMin) * 0.08;
    const yMinPadded = rawMin >= 0 ? Math.max(0, yMin - yPad) : yMin - yPad;
    return { xMin, xMax, yMin: yMinPadded, yMax: yMax + yPad };
  });

  readonly rendered = computed<RenderedSeries[]>(() => {
    const bounds = this.bounds();

    return this.series().map((item) => ({
      ...item,
      dots: item.points.map((point) => this.project(point, bounds)),
    }));
  });

  readonly xGrid = computed<Tick[]>(() => {
    const bounds = this.bounds();
    const ticks: Tick[] = [];

    for (let index = 0; index <= 4; index++) {
      const value = bounds.xMin + ((bounds.xMax - bounds.xMin) * index) / 4;
      ticks.push({
        value,
        x: this.scaleX(value, bounds),
        y: HEIGHT - PAD_BOTTOM,
        label: formatAxis(value, this.xInteger()),
      });
    }

    return ticks;
  });

  readonly yGrid = computed<Tick[]>(() => {
    const bounds = this.bounds();
    const ticks: Tick[] = [];

    for (let index = 0; index <= 4; index++) {
      const value = bounds.yMin + ((bounds.yMax - bounds.yMin) * index) / 4;
      ticks.push({
        value,
        x: PAD_LEFT,
        y: this.scaleY(value, bounds),
        label: formatAxis(value),
      });
    }

    return ticks;
  });

  readonly markerPoint = computed<ChartPoint | null>(() => {
    const marker = this.markerX();
    if (marker === null || !Number.isFinite(marker)) {
      return null;
    }

    return { x: this.scaleX(marker, this.bounds()), y: HEIGHT - PAD_BOTTOM };
  });

  pointsAttr(points: ChartPoint[]): string {
    return points.map((point) => `${point.x.toFixed(1)},${point.y.toFixed(1)}`).join(' ');
  }

  private project(point: ChartPoint, bounds: { xMin: number; xMax: number; yMin: number; yMax: number }): ChartPoint {
    return { x: this.scaleX(point.x, bounds), y: this.scaleY(point.y, bounds) };
  }

  private scaleX(value: number, bounds: { xMin: number; xMax: number }): number {
    const ratio = (value - bounds.xMin) / (bounds.xMax - bounds.xMin);
    return PAD_LEFT + ratio * (WIDTH - PAD_LEFT - PAD_RIGHT);
  }

  private scaleY(value: number, bounds: { yMin: number; yMax: number }): number {
    const ratio = (value - bounds.yMin) / (bounds.yMax - bounds.yMin);
    return HEIGHT - PAD_BOTTOM - ratio * (HEIGHT - PAD_TOP - PAD_BOTTOM);
  }
}

function formatAxis(value: number, integer = false): string {
  if (integer) {
    return String(Math.round(value));
  }

  if (Math.abs(value) >= 1000) {
    return `${Math.round(value / 100) / 10}k`;
  }
  if (Math.abs(value) >= 100) {
    return String(Math.round(value));
  }
  return String(Math.round(value * 10) / 10);
}
