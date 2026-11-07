import {
  ChangeDetectionStrategy,
  Component,
  ElementRef,
  computed,
  effect,
  inject,
  output,
  signal,
  viewChild,
} from '@angular/core';
import { DeviceService } from '../core/device.service';

/** Адрес облака, где живёт «Люси». Тот же сервис, что и в DED CORE CONTROL. */
const CLOUD = 'https://dedbox.pro';

type ChatRole = 'user' | 'assistant' | 'error';

interface ChatMessage {
  role: ChatRole;
  text: string;
}

/** Почему чат недоступен. Порядок важен: показываем самую первую причину. */
type Gate = 'checking' | 'no-wifi' | 'not-paired' | 'no-cloud' | 'ready';

@Component({
  selector: 'ded-assistant',
  standalone: true,
  changeDetection: ChangeDetectionStrategy.OnPush,
  templateUrl: './assistant.component.html',
  styleUrl: './assistant.component.css',
})
export class AssistantComponent {
  private readonly device = inject(DeviceService);

  /** Переключить вкладку «Сеть»: оттуда настраивается домашняя сеть и привязка. */
  readonly openNetwork = output<void>();

  readonly messages = signal<ChatMessage[]>([]);
  readonly prompt = signal('');
  readonly busy = signal(false);
  readonly cloud = signal<'unknown' | 'checking' | 'ok' | 'fail'>('unknown');

  private readonly chatEnd = viewChild<ElementRef<HTMLElement>>('chatEnd');
  private checkedFor = '';

  readonly wifiConnected = computed(() => {
    const status = this.device.deviceStatus();
    const telemetry = this.device.telemetry();

    return status?.wifiConnected === true || telemetry?.wifi_connected === 1;
  });

  readonly paired = computed(() => this.device.deviceStatus()?.paired === true);
  readonly mac = computed(() => this.device.deviceStatus()?.mac ?? '');

  readonly gate = computed<Gate>(() => {
    if (!this.wifiConnected()) {
      return 'no-wifi';
    }

    if (!this.paired() || this.mac() === '') {
      return 'not-paired';
    }

    switch (this.cloud()) {
      case 'ok':
        return 'ready';
      case 'fail':
        return 'no-cloud';
      default:
        return 'checking';
    }
  });

  readonly gateTitle = computed(() => {
    switch (this.gate()) {
      case 'no-wifi':
        return 'Нет домашней сети';
      case 'not-paired':
        return 'Блок не привязан к учётной записи';
      case 'no-cloud':
        return 'Нет доступа к сервису «Люси»';
      case 'checking':
        return 'Проверяю доступ к «Люси»';
      default:
        return '';
    }
  });

  readonly gateHint = computed(() => {
    switch (this.gate()) {
      case 'no-wifi':
        return '«Люси» отвечает через интернет, поэтому блок должен быть подключён к домашней сети. Откройте «Сеть и обновления» и укажите сеть с интернетом.';
      case 'not-paired':
        return 'Чат работает от имени владельца блока. Привяжите блок в DED BOX по MAC и коду сопряжения — код показан на вкладке «Сеть и обновления».';
      case 'no-cloud':
        return 'Блок в сети, но браузер не достучался до dedbox.pro. Проверьте интернет на телефоне и откройте страницу блока по его адресу в домашней сети.';
      case 'checking':
        return 'Отправляю запрос в сервис помощницы…';
      default:
        return '';
    }
  });

  /** Короткие вопросы, с которых удобно начать. */
  readonly suggestions: ReadonlyArray<string> = [
    'Почему блок дёргается на холостых?',
    'Какие углы поставить для 8000 об/мин?',
    'Что проверить, если пропала искра?',
  ];

  constructor() {
    // Повторная проверка: сеть и привязка меняются на других вкладках, и вкладка чата
    // должна оживать сразу, без перезагрузки страницы.
    effect(() => {
      const key = `${this.wifiConnected()}|${this.paired()}|${this.mac()}`;

      if (key === this.checkedFor) {
        return;
      }

      this.checkedFor = key;

      if (!this.wifiConnected() || !this.paired() || this.mac() === '') {
        this.cloud.set('unknown');
        return;
      }

      void this.checkCloud();
    });
  }

  async checkCloud(): Promise<void> {
    const mac = this.mac();
    if (!mac) {
      this.cloud.set('fail');
      return;
    }

    this.cloud.set('checking');

    try {
      const response = await this.post<{ nonce?: string }>(`${CLOUD}/api/blog/ai/device/challenge`, { mac }, 15000);
      this.cloud.set(response?.nonce ? 'ok' : 'fail');
    } catch {
      this.cloud.set('fail');
    }
  }

  useSuggestion(text: string): void {
    this.prompt.set(text);
    void this.send();
  }

  async send(): Promise<void> {
    const question = this.prompt().trim();
    if (question === '' || this.busy() || this.gate() !== 'ready') {
      return;
    }

    const mac = this.mac();
    this.prompt.set('');
    this.busy.set(true);
    this.push({ role: 'user', text: question });

    try {
      const challenge = await this.post<{ nonce?: string }>(`${CLOUD}/api/blog/ai/device/challenge`, { mac }, 15000);
      if (!challenge?.nonce) {
        throw new Error('Сервис «Люси» не выдал запрос на подпись.');
      }

      // Подпись считает сам блок: ключ устройства из браузера недоступен и не должен быть.
      const grant = await this.post<{ mac?: string; signature?: string }>(
        '/api/ai/grant',
        { nonce: challenge.nonce },
        10000,
      );

      if (!grant?.signature) {
        throw new Error('Блок не подтвердил привязку. Проверьте привязку в DED BOX.');
      }

      const answer = await this.post<{ response?: string }>(
        `${CLOUD}/api/blog/ai/device/chat`,
        {
          mac: grant.mac ?? mac,
          nonce: challenge.nonce,
          signature: grant.signature,
          prompt: this.buildPrompt(question),
        },
        90000,
      );

      this.push({ role: 'assistant', text: answer?.response?.trim() || 'Пустой ответ модели.' });
    } catch (error) {
      this.push({
        role: 'error',
        text: error instanceof Error ? error.message : 'Помощница не ответила. Повторите позже.',
      });
    } finally {
      this.busy.set(false);
    }
  }

  onKeydown(event: KeyboardEvent): void {
    if (event.key === 'Enter' && !event.shiftKey) {
      event.preventDefault();
      void this.send();
    }
  }

  private push(message: ChatMessage): void {
    this.messages.update((list) => [...list, message]);
    setTimeout(() => this.chatEnd()?.nativeElement.scrollIntoView({ block: 'end', behavior: 'smooth' }), 0);
  }

  /**
   * Состояние блока в вопросе: модель не видит телеметрию сама, а без неё советы
   * «проверьте угол» бесполезны.
   */
  private buildPrompt(question: string): string {
    const info = this.device.info();
    const telemetry = this.device.telemetry();

    const live = telemetry
      ? `Текущие показания: обороты ${telemetry.rpm}, дроссель ${telemetry.throttle}%, температура ${telemetry.termo}°C, карта ${telemetry.card}, двигатель ${telemetry.motor_running === 1 ? 'работает' : 'заглушён'}.`
      : 'Телеметрии нет: страница блока не получает данные.';

    const setup = telemetry
      ? `Настройки блока: ограничение ${telemetry.max_rpm} об/мин, начальный угол ${telemetry.init_angle}°, угол опережения ${telemetry.advance_angle}°, серво ${telemetry.servo === 1 ? 'включено' : 'выключено'}.`
      : '';

    return [
      'Ты инженер по настройке двигателя DED CORE. Отвечай по-русски, кратко и по делу, опираясь только на приведённые данные.',
      `Блок DED CORE V4, прошивка ${info?.firmware_version || this.device.firmwareVersion()}.`,
      live,
      setup,
      `Вопрос владельца: ${question}`,
    ]
      .filter((line) => line !== '')
      .join('\n')
      .slice(0, 1900);
  }

  private async post<T>(url: string, body: unknown, timeoutMs: number): Promise<T | null> {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);

    try {
      const response = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
        credentials: 'omit',
        cache: 'no-store',
        signal: controller.signal,
      });

      if (!response.ok) {
        const payload = await response.json().catch(() => null);
        throw new Error(payload?.error || `Сервис ответил ${response.status}.`);
      }

      return (await response.json()) as T;
    } catch (error) {
      if (error instanceof DOMException && error.name === 'AbortError') {
        throw new Error('Сервис не ответил вовремя. Проверьте интернет и повторите.');
      }

      throw error instanceof Error ? error : new Error('Запрос не удался.');
    } finally {
      clearTimeout(timer);
    }
  }
}
