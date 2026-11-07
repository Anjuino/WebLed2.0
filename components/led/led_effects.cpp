#include "led_effects.h"
#include "esp_random.h"
#include <math.h>
#include <algorithm>

static uint32_t frame_ms(led *self, uint32_t slow_ms, uint32_t fast_ms)
{
  uint32_t sp = std::min<uint32_t>(self->get_speed(), 100);
  uint32_t ms = slow_ms - (slow_ms - fast_ms) * sp / 100;
  return std::max<uint32_t>(ms, 10);
}

static void set_level(led *self, uint16_t i, uint8_t level)
{
  auto [r, g, b] = self->get_color();
  self->set_pixel(i, r * level / 255, g * level / 255, b * level / 255);
}

static void ensure(std::vector<uint8_t> &buf, uint16_t count)
{
  if (buf.size() != count) buf.assign(count, 0);
}

static void fade_all(std::vector<uint8_t> &buf, uint8_t keep)
{
  for (auto &v : buf) v = v * keep / 256;
}

static uint32_t rnd(uint32_t n) { return n ? esp_random() % n : 0; }

static void rainbow(led *self, void *)
{
  static uint16_t rainbow_offset = 0;
  uint16_t count = self->get_count_led();

  for (uint16_t i = 0; i < count; i++) {
    uint16_t hue = (i * 255 / count + rainbow_offset) % 255;
    uint8_t r, g, b;

    if (hue < 85) {
      r = 255 - hue * 3;
      g = hue * 3;
      b = 0;
    } else if (hue < 170) {
      hue -= 85;
      r = 0;
      g = 255 - hue * 3;
      b = hue * 3;
    } else {
      hue -= 170;
      r = hue * 3;
      g = 0;
      b = 255 - hue * 3;
    }

    self->set_pixel(i, r, g, b);
  }

  rainbow_offset = (rainbow_offset + 1) % 255;
  self->show();
  self->wait(frame_ms(self, 110, 10));
}

static void static_color(led *self, void *)
{
  auto [r, g, b] = self->get_color();
  uint16_t count = self->get_count_led();
  for (uint16_t i = 0; i < count; i++) {
    self->set_pixel(i, r, g, b);
  }
  self->show();
}

static void off(led *self, void *)
{
  self->off();
}

static void manual(led *self, void *)
{
  self->show();
}

static void comet(led *self, void *)
{
  static std::vector<uint8_t> lvl;
  static uint16_t head = 0;
  uint16_t count = self->get_count_led();
  ensure(lvl, count);
  if (head >= count) head = 0;

  fade_all(lvl, 200);
  lvl[head] = 255;
  head = (head + 1) % count;

  for (uint16_t i = 0; i < count; i++) set_level(self, i, lvl[i]);
  self->show();
  self->wait(frame_ms(self, 120, 10));
}

static void fire(led *self, void *)
{
  static std::vector<uint8_t> heat;
  uint16_t count = self->get_count_led();
  ensure(heat, count);

  const uint32_t cooling = 55, sparking = 120;
  for (uint16_t i = 0; i < count; i++) {
    uint32_t cool = rnd(cooling * 10 / count + 2);
    heat[i] = heat[i] > cool ? heat[i] - cool : 0;
  }
  for (uint16_t i = count - 1; i >= 2; i--) {
    heat[i] = (heat[i - 1] + heat[i - 2] * 2) / 3;
  }
  if (rnd(256) < sparking) {
    uint16_t y = rnd(std::min<uint16_t>(7, count));
    heat[y] = std::min<uint32_t>(255, heat[y] + 160 + rnd(96));
  }

  auto [r, g, b] = self->get_color();
  for (uint16_t i = 0; i < count; i++) {
    uint8_t t = heat[i];
    if (t < 128) {
      self->set_pixel(i, r * t / 128, g * t / 128, b * t / 128);
    } else {
      uint8_t w = (t - 128) * 2;
      self->set_pixel(i, r + (255 - r) * w / 255, g + (255 - g) * w / 255, b + (255 - b) * w / 255);
    }
  }
  self->show();
  self->wait(frame_ms(self, 120, 15));
}

static void breathe(led *self, void *)
{
  static uint16_t phase = 0;   // 0..511
  uint16_t count = self->get_count_led();

  uint16_t tri = phase < 256 ? phase : 511 - phase;
  uint8_t level = std::max<uint32_t>(6, tri * tri / 255);
  for (uint16_t i = 0; i < count; i++) set_level(self, i, level);
  self->show();

  phase = (phase + 1 + self->get_speed() * 7 / 100) % 512;
  self->wait(20);
}

static void twinkle(led *self, void *)
{
  static std::vector<uint8_t> lvl;
  uint16_t count = self->get_count_led();
  ensure(lvl, count);

  fade_all(lvl, 235);
  uint16_t sparks = std::max<uint16_t>(1, count / 50);
  for (uint16_t n = 0; n < sparks; n++) {
    if (rnd(100) < 40) lvl[rnd(count)] = 255;
  }

  for (uint16_t i = 0; i < count; i++) set_level(self, i, lvl[i]);
  self->show();
  self->wait(frame_ms(self, 100, 15));
}

static void chase(led *self, void *)
{
  static uint8_t offset = 0;
  uint16_t count = self->get_count_led();

  for (uint16_t i = 0; i < count; i++) set_level(self, i, (i + offset) % 3 == 0 ? 255 : 0);
  offset = (offset + 1) % 3;
  self->show();
  self->wait(frame_ms(self, 400, 40));
}

static void wave(led *self, void *)
{
  static float phase = 0;
  uint16_t count = self->get_count_led();
  const float wavelength = 24.0f;   // светодиодов на период

  for (uint16_t i = 0; i < count; i++) {
    float s = sinf((i / wavelength + phase) * 2.0f * (float)M_PI);   // -1..1
    set_level(self, i, (uint8_t)(10 + (s + 1.0f) * 0.5f * 245));
  }
  self->show();

  phase += 0.005f + self->get_speed() * 0.0004f;
  if (phase >= 1.0f) phase -= 1.0f;
  self->wait(20);
}

static void wipe(led *self, void *)
{
  static uint32_t pos = 0;   // 0..2*count: первая половина — зажигаем, вторая — гасим
  uint16_t count = self->get_count_led();
  if (pos >= 2u * count) pos = 0;

  bool filling = pos < count;
  uint16_t edge = filling ? pos : pos - count;
  for (uint16_t i = 0; i < count; i++) {
    bool lit = filling ? i <= edge : i > edge;
    set_level(self, i, lit ? 255 : 0);
  }
  pos++;
  self->show();
  self->wait(frame_ms(self, 150, 10));
}

static void scanner(led *self, void *)
{
  static std::vector<uint8_t> lvl;
  static int32_t pos = 0, dir = 1;
  uint16_t count = self->get_count_led();
  ensure(lvl, count);
  if (pos >= count) pos = count - 1;

  fade_all(lvl, 180);
  lvl[pos] = 255;
  if (count > 1) {
    if (pos + dir < 0 || pos + dir >= count) dir = -dir;
    pos += dir;
  }

  for (uint16_t i = 0; i < count; i++) set_level(self, i, lvl[i]);
  self->show();
  self->wait(frame_ms(self, 100, 10));
}

// ---------------------------------------------------------------------------
// Цветные эффекты. Рисуют в общий RGB-холст, а не в уровни одного цвета:
// эффекты выполняются только в задаче Led и по одному, поэтому холст
// можно делить. При смене эффекта остатки старого просто дотухают.
// ---------------------------------------------------------------------------

struct rgb8_t { uint8_t r, g, b; };
static std::vector<rgb8_t> canvas;

static void ensure_canvas(uint16_t count)
{
  if (canvas.size() != count) canvas.assign(count, {0, 0, 0});
}

static void fade_px(rgb8_t &c, uint8_t keep)
{
  c.r = c.r * keep >> 8;
  c.g = c.g * keep >> 8;
  c.b = c.b * keep >> 8;
}

static void fade_canvas(uint8_t keep)
{
  for (auto &c : canvas) fade_px(c, keep);
}

static void add_px(rgb8_t &c, rgb8_t a)
{
  c.r = std::min(255, c.r + a.r);
  c.g = std::min(255, c.g + a.g);
  c.b = std::min(255, c.b + a.b);
}

static void draw_canvas(led *self)
{
  for (uint16_t i = 0; i < canvas.size(); i++) self->set_pixel(i, canvas[i].r, canvas[i].g, canvas[i].b);
  self->show();
}

// цветовой круг: h 0..255, s — насыщенность, v — яркость
static rgb8_t hsv(uint8_t h, uint8_t s = 255, uint8_t v = 255)
{
  uint8_t region = h / 43;
  uint8_t rem = (h - region * 43) * 6;
  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * rem) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
  switch (region) {
    case 0:  return {v, t, p};
    case 1:  return {q, v, p};
    case 2:  return {p, v, t};
    case 3:  return {p, q, v};
    case 4:  return {t, p, v};
    default: return {v, p, q};
  }
}

// множитель скорости анимации: speed 0..100 -> 0.3..3.0
static float speed_k(led *self)
{
  return 0.3f + std::min<uint32_t>(self->get_speed(), 100) * 0.027f;
}

static void wrap_phase(float &phase)
{
  if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
}

// разноцветные искры вспыхивают в случайных местах и гаснут
static void confetti(led *self, void *)
{
  static uint8_t hue = 0;
  uint16_t count = self->get_count_led();
  ensure_canvas(count);

  fade_canvas(225);
  uint16_t sparks = 1 + count / 40;
  for (uint16_t n = 0; n < sparks; n++) {
    if (rnd(100) < 60) add_px(canvas[rnd(count)], hsv(hue + rnd(64), 200, 255));
  }
  hue++;

  draw_canvas(self);
  self->wait(frame_ms(self, 60, 10));
}

// точка качается туда-обратно по синусу, меняя цвет, и оставляет шлейф
static void sinelon(led *self, void *)
{
  static float phase = 0;
  static uint8_t hue = 0;
  static uint16_t last = 0;
  uint16_t count = self->get_count_led();
  ensure_canvas(count);

  fade_canvas(220);
  uint16_t pos = (uint16_t)((sinf(phase) + 1.0f) * 0.5f * (count - 1));
  if (last >= count) last = pos;
  // на большой скорости точка перескакивает через пиксели — заливаем промежуток
  for (uint16_t i = std::min(pos, last); i <= std::max(pos, last); i++) add_px(canvas[i], hsv(hue));
  last = pos;

  phase += 0.03f * speed_k(self);
  wrap_phase(phase);
  hue++;

  draw_canvas(self);
  self->wait(16);
}

// восемь цветных точек с разной частотой — переплетаются друг с другом
static void juggle(led *self, void *)
{
  static float t = 0;
  uint16_t count = self->get_count_led();
  ensure_canvas(count);

  fade_canvas(215);
  for (int k = 0; k < 8; k++) {
    uint16_t pos = (uint16_t)((sinf(t * (k + 7) * 0.13f) + 1.0f) * 0.5f * (count - 1));
    add_px(canvas[pos], hsv(k * 32, 220, 255));
  }
  t += 0.08f * speed_k(self);

  draw_canvas(self);
  self->wait(16);
}

// цветные полосы пульсируют в такт
static void bpm(led *self, void *)
{
  static float phase = 0;
  static uint8_t hue = 0;
  uint16_t count = self->get_count_led();
  ensure_canvas(count);

  uint8_t beat = (uint8_t)(64 + (sinf(phase) + 1.0f) * 0.5f * 191);
  for (uint16_t i = 0; i < count; i++) {
    canvas[i] = hsv(hue + i * 2, 255, (uint8_t)(beat - hue + i * 10));
  }
  phase += 0.08f * speed_k(self);
  wrap_phase(phase);
  hue++;

  draw_canvas(self);
  self->wait(20);
}

// переливающаяся плазма из трёх синусов — по всей ленте сразу
static void plasma(led *self, void *)
{
  static float t = 0;
  uint16_t count = self->get_count_led();
  ensure_canvas(count);

  for (uint16_t i = 0; i < count; i++) {
    float x = i / 12.0f;
    float v = sinf(x + t) + sinf(x * 0.7f - t * 1.3f) + sinf(x * 0.3f + t * 0.7f);   // -3..3
    canvas[i] = hsv((uint8_t)((v + 3.0f) / 6.0f * 255.0f + t * 8.0f));
  }
  t += 0.03f * speed_k(self);
  if (t > 1000.0f) t = 0;

  draw_canvas(self);
  self->wait(20);
}

// метеор с рваным, неравномерно гаснущим хвостом; каждый пролёт — новый цвет
static void meteor(led *self, void *)
{
  static int32_t pos = 0;
  static uint8_t hue = 0;
  uint16_t count = self->get_count_led();
  ensure_canvas(count);

  int32_t size = std::max<int32_t>(2, count / 25);
  for (auto &c : canvas) {
    if (rnd(10) > 4) fade_px(c, 170);
  }
  for (int32_t j = 0; j < size; j++) {
    int32_t idx = pos - j;
    if (idx >= 0 && idx < count) canvas[idx] = hsv(hue, 200, 255 - j * 180 / size);
  }

  pos++;
  if (pos >= count + count / 2) {   // даём хвосту догореть за краем ленты
    pos = 0;
    hue += 47;
  }

  draw_canvas(self);
  self->wait(frame_ms(self, 60, 8));
}

// прыгающие мячики с гравитацией, у каждого своя упругость
static void bouncing_balls(led *self, void *)
{
  const int N = 4;
  const float gravity = 9.81f;
  const float start_v = 4.43f;   // sqrt(2 * g * 1) — подлёт на всю высоту ленты
  static float height[N], velocity[N];
  static bool started = false;
  if (!started) {
    for (int k = 0; k < N; k++) {
      height[k] = 0;
      velocity[k] = start_v * (1.0f - k * 0.07f);
    }
    started = true;
  }

  uint16_t count = self->get_count_led();
  ensure_canvas(count);
  fade_canvas(120);

  float dt = 0.012f * speed_k(self);
  for (int k = 0; k < N; k++) {
    velocity[k] -= gravity * dt;
    height[k] += velocity[k] * dt;
    if (height[k] < 0) {
      height[k] = 0;
      velocity[k] = -velocity[k] * (0.90f - k * 0.03f);
      if (velocity[k] < 1.0f) velocity[k] = start_v * (0.85f + rnd(16) / 100.0f);   // выдохся — подкидываем
    }
    uint16_t p = (uint16_t)(std::min(height[k], 1.0f) * (count - 1));
    add_px(canvas[p], hsv(k * 64 + 20));
  }

  draw_canvas(self);
  self->wait(16);
}

// круги на воде: из случайной точки расходится цветная волна
static void ripple(led *self, void *)
{
  static int32_t center = -1;
  static int32_t step = 0;
  static uint8_t hue = 0;
  uint16_t count = self->get_count_led();
  ensure_canvas(count);

  fade_canvas(200);
  int32_t max_step = std::max<int32_t>(8, count / 4);
  if (center < 0 || center >= count || step >= max_step) {
    center = rnd(count);
    step = 0;
    hue = rnd(256);
  }

  uint8_t v = 255 * (max_step - step) / max_step;
  if (center + step < count) add_px(canvas[center + step], hsv(hue, 230, v));
  if (center - step >= 0)    add_px(canvas[center - step], hsv(hue, 230, v));
  step++;

  draw_canvas(self);
  self->wait(frame_ms(self, 80, 15));
}

// «театральные огни» — бегут через один, переливаясь радугой
static void theater_rainbow(led *self, void *)
{
  static uint8_t q = 0;
  static uint8_t hue = 0;
  uint16_t count = self->get_count_led();
  ensure_canvas(count);

  for (uint16_t i = 0; i < count; i++) {
    canvas[i] = (i + q) % 3 == 0 ? hsv(hue + i * 4) : rgb8_t{0, 0, 0};
  }
  q = (q + 1) % 3;
  hue += 3;

  draw_canvas(self);
  self->wait(frame_ms(self, 200, 30));
}

// лента заливается новым цветом поверх старого, цвет меняется каждый проход
static void color_wipe(led *self, void *)
{
  static uint16_t pos = 0;
  static uint8_t hue = 0;
  uint16_t count = self->get_count_led();
  ensure_canvas(count);

  if (pos >= count) {
    pos = 0;
    hue += 50;
  }
  canvas[pos++] = hsv(hue);

  draw_canvas(self);
  self->wait(frame_ms(self, 80, 5));
}

// мигалка: две вспышки красным на одной половине, две синим на другой
static void police(led *self, void *)
{
  static uint8_t step = 0;
  uint16_t count = self->get_count_led();
  ensure_canvas(count);

  bool on = step % 2 == 0;
  bool red = step < 4;
  for (uint16_t i = 0; i < count; i++) {
    bool left = i < count / 2;
    if (on && red && left)        canvas[i] = {255, 0, 0};
    else if (on && !red && !left) canvas[i] = {0, 0, 255};
    else                          canvas[i] = {0, 0, 0};
  }
  step = (step + 1) % 8;

  draw_canvas(self);
  self->wait(frame_ms(self, 200, 70));
}

void register_builtin_effects(led *self)
{
  self->add_effect(1,   "Радуга",         true,  &rainbow);
  self->add_effect(2,   "Бегущий огонь",  true,  &comet);
  self->add_effect(3,   "Огонь",          true,  &fire);
  self->add_effect(4,   "Дыхание",        true,  &breathe);
  self->add_effect(5,   "Мерцание",       true,  &twinkle);
  self->add_effect(6,   "Погоня",         true,  &chase);
  self->add_effect(7,   "Волна",          true,  &wave);
  self->add_effect(8,   "Заполнение",     true,  &wipe);
  self->add_effect(9,   "Сканер",         true,  &scanner);
  self->add_effect(10,  "Конфетти",       true,  &confetti);
  self->add_effect(11,  "Маятник",        true,  &sinelon);
  self->add_effect(12,  "Жонглёр",        true,  &juggle);
  self->add_effect(13,  "Пульс",          true,  &bpm);
  self->add_effect(14,  "Плазма",         true,  &plasma);
  self->add_effect(15,  "Метеор",         true,  &meteor);
  self->add_effect(16,  "Мячики",         true,  &bouncing_balls);
  self->add_effect(17,  "Круги на воде",  true,  &ripple);
  self->add_effect(18,  "Театр",          true,  &theater_rainbow);
  self->add_effect(19,  "Цветная заливка", true, &color_wipe);
  self->add_effect(20,  "Мигалка",        true,  &police);
  self->add_effect(253, "Ручной",         false, &manual);
  self->add_effect(254, "Статичный цвет", false, &static_color);
  self->add_effect(255, "Выкл",           false, &off);
}
