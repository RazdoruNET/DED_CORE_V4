#ifndef IMAGE_HASH_H
#define IMAGE_HASH_H

// Контрольные суммы образа прошивки: считаем потоком, пока образ читается из сети
// или пишется во флеш, — вторая копия образа в памяти не нужна.
//
// SHA-256 основной: по нему проверяем целостность, если сервис его прислал.
// MD5 остаётся только для обратной совместимости — блоки со старой прошивкой
// другого поля не знают, да и образ им отдаёт тот же сервис.

#include <Arduino.h>
#include <MD5Builder.h>
#include <mbedtls/sha256.h>

class ImageHasher {
 public:
  void begin() {
    md5Builder.begin();
    mbedtls_sha256_init(&sha256Context);
    mbedtls_sha256_starts_ret(&sha256Context, 0);  // 0 — SHA-256, не SHA-224
  }

  void add(const uint8_t *data, size_t len) {
    if (data == nullptr || len == 0) {
      return;
    }

    md5Builder.add((uint8_t *)data, len);
    mbedtls_sha256_update_ret(&sha256Context, data, len);
  }

  String md5() {
    MD5Builder copy = md5Builder;
    copy.calculate();
    String value = copy.toString();
    value.toLowerCase();
    return value;
  }

  String sha256() {
    mbedtls_sha256_context copy = sha256Context;
    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&copy, digest);

    char hex[65];
    for (uint8_t i = 0; i < 32; i++) {
      snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }

    return String(hex);
  }

  // Сверка с тем, что прислал сервис: SHA-256, а при его отсутствии — MD5.
  // Ни одной суммы не пришло — считаем проверку невыполненной: образ без
  // подтверждения целостности во флеш не пишем.
  bool matches(const String &expectedSha256, const String &expectedMd5, String &error) {
    if (expectedSha256.length() == 64) {
      if (sha256() != lower(expectedSha256)) {
        error = "SHA-256 образа не совпал";
        return false;
      }

      return true;
    }

    if (expectedMd5.length() == 32) {
      if (md5() != lower(expectedMd5)) {
        error = "MD5 образа не совпал";
        return false;
      }

      Serial.println("Image: sha256 not provided, checked md5");
      return true;
    }

    error = "сервис не передал контрольную сумму образа";
    return false;
  }

  static String lower(String value) {
    value.toLowerCase();
    return value;
  }

 private:
  MD5Builder md5Builder;
  mbedtls_sha256_context sha256Context;
};

// Обёртка над потоком: считает суммы по тем байтам, которые из него прочитали.
// Нужна, чтобы не переписывать проверенный цикл записи образа в библиотеке
// обновления — он просто получает наш поток вместо сетевого.
class HashingStream : public Stream {
 public:
  HashingStream(Stream &source, ImageHasher &hasher) : source(source), hasher(hasher) {}

  int available() override { return source.available(); }
  int read() override { return source.read(); }
  int peek() override { return source.peek(); }

  size_t readBytes(char *buffer, size_t length) override {
    size_t read = source.readBytes(buffer, length);
    hasher.add((const uint8_t *)buffer, read);
    return read;
  }

  size_t readBytes(uint8_t *buffer, size_t length) override {
    size_t read = source.readBytes(buffer, length);
    hasher.add(buffer, read);
    return read;
  }

  void flush() override {}
  size_t write(uint8_t) override { return 0; }
  size_t write(const uint8_t *, size_t) override { return 0; }

 private:
  Stream &source;
  ImageHasher &hasher;
};

#endif
