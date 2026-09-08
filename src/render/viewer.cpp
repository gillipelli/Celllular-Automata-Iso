#include "ashfall/core/iso.hpp"
#include "ashfall/sim/world.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>
#include <raylib.h>
#include <string>
#include <vector>
using namespace ashfall;
namespace {
constexpr Color BACKGROUND{13, 20, 26, 255}, PANEL{20, 30, 38, 255}, INK{217, 227, 218, 255},
    MUTED{132, 154, 156, 255}, ACCENT{226, 180, 103, 255};
constexpr int CHUNK = 32, TW = 1060, TH = 730, OX = 530, OY = 160;
struct Cache {
  RenderTexture2D texture{};
  bool dirty = true;
  uint64_t last = 0;
  float scale = 0;
};
Color shade(Color c, float factor) {
  return {static_cast<unsigned char>(c.r * factor), static_cast<unsigned char>(c.g * factor),
          static_cast<unsigned char>(c.b * factor), c.a};
}
void diamond(float x, float y, float w, float h, Color c) {
  DrawTriangle({x, y - h}, {x - w, y}, {x, y + h}, c);
  DrawTriangle({x, y - h}, {x, y + h}, {x + w, y}, c);
}
Point rotate(int x, int y, int n, int rotation) {
  switch (rotation % 4) {
  case 1:
    return {static_cast<float>(n - 1 - y), static_cast<float>(x)};
  case 2:
    return {static_cast<float>(n - 1 - x), static_cast<float>(n - 1 - y)};
  case 3:
    return {static_cast<float>(y), static_cast<float>(n - 1 - x)};
  default:
    return {static_cast<float>(x), static_cast<float>(y)};
  }
}
Point original(int x, int y, int n, int rotation) { return rotate(x, y, n, (4 - rotation) % 4); }
void cell(const World &world, int x, int y, float sx, float sy, int overlay) {
  const int i = y * world.config().world.size + x;
  const auto &t = world.terrain();
  const auto &f = world.forest();
  const auto kind = t.terrain[i];
  const float elevation = static_cast<float>(t.elevation[i]) * 2;
  sy -= elevation;
  Color ground = kind == WATER   ? Color{40, 78, 96, 255}
                 : kind == PLAIN ? Color{106, 122, 74, 255}
                 : kind == HILL  ? Color{114, 116, 78, 255}
                                 : Color{128, 126, 111, 255};
  if (overlay) {
    const float value =
        static_cast<float>((overlay == 1 ? t.moisture[i] : f.fertility()[i]).to_double());
    ground = {static_cast<unsigned char>(45 + value * 55),
              static_cast<unsigned char>(55 + value * 140),
              static_cast<unsigned char>(70 + value * 100), 255};
  }
  diamond(sx, sy + 5, 16, 8, shade(ground, .6f));
  diamond(sx, sy, 16, 8, ground);
  if (overlay)
    return;
  if (f.state()[i] == TREE || f.state()[i] == SAPLING) {
    const bool sapling = f.state()[i] == SAPLING;
    const float height = sapling ? 4 + static_cast<float>(f.age()[i]) * 14 /
                                           static_cast<float>(world.config().forest.t_mature)
                                 : 20;
    DrawLineEx({sx, sy}, {sx, sy - height}, 2, Color{72, 67, 44, 255});
    const Color foliage = sapling ? Color{87, 148, 83, 255} : Color{44, 99, 66, 255};
    DrawTriangle({sx, sy - height - 5}, {sx - 10, sy - 3}, {sx + 9, sy - 3}, shade(foliage, .9f));
    DrawTriangle({sx, sy - height - 5}, {sx, sy - 3}, {sx + 9, sy - 3}, shade(foliage, 1.2f));
  } else if (f.state()[i] == BURNING) {
    DrawTriangle({sx, sy - 27}, {sx - 9, sy}, {sx + 9, sy}, Color{219, 90, 39, 255});
    DrawTriangle({sx + 1, sy - 18}, {sx - 5, sy}, {sx + 5, sy}, Color{255, 202, 83, 255});
  } else if (f.state()[i] == ASH)
    diamond(sx, sy, 12, 5, Color{61, 62, 59, 255});
  const float smoke = static_cast<float>(f.smoke()[i].to_double());
  if (smoke > .08f)
    DrawCircleV({sx + smoke * 3, sy - 28 - smoke * 12}, 3 + smoke * 3,
                Fade(Color{153, 151, 138, 255}, smoke * .45f));
}
Color kingdom_color(int k) {
  constexpr std::array<Color, 12> colors = {
      Color{229, 165, 86, 255},  Color{104, 177, 216, 255}, Color{191, 120, 198, 255},
      Color{132, 191, 119, 255}, Color{232, 116, 110, 255}, Color{224, 213, 125, 255},
      Color{98, 193, 178, 255},  Color{164, 155, 226, 255}, Color{223, 155, 179, 255},
      Color{143, 161, 98, 255},  Color{137, 174, 184, 255}, Color{195, 151, 122, 255}};
  return colors[static_cast<size_t>(k) % colors.size()];
}
void society(const World &world, int rotation) {
  const auto *civ = world.civilization();
  if (!civ)
    return;
  const int n = world.config().world.size;
  for (int d = 0; d < 2 * n - 1; ++d)
    for (int x = std::max(0, d - n + 1); x <= std::min(n - 1, d); ++x) {
      const int y = d - x;
      const auto original_cell = original(x, y, n, rotation);
      const int cell = static_cast<int>(original_cell.y) * n + static_cast<int>(original_cell.x);
      const int owner = civ->owner()[cell];
      const auto pos = project(static_cast<float>(x), static_cast<float>(y),
                               static_cast<float>(world.terrain().elevation[cell]) * .25f);
      if (owner >= 0) {
        const float pop = static_cast<float>(civ->layers(owner).pop.front()[cell].to_double());
        if (pop > .05f) {
          const auto color = kingdom_color(owner);
          diamond(pos.x, pos.y, 12, 5, Fade(color, .22f));
          DrawLineEx({pos.x, pos.y}, {pos.x, pos.y - pop * 22}, 3, Fade(color, .7f));
        }
      }
      const float refugees = static_cast<float>(civ->refugees()[cell].to_double());
      if (refugees > .02f)
        DrawLineEx({pos.x + 4, pos.y}, {pos.x + 4, pos.y - std::min(32.f, refugees * 25)}, 3,
                   Color{201, 202, 190, 220});
    }
  for (const auto &node : civ->nodes())
    if (!node.destroyed) {
      const auto r = rotate(node.cell % n, node.cell / n, n, rotation),
                 p = project(r.x, r.y,
                             static_cast<float>(world.terrain().elevation[node.cell]) * .25f);
      const float height = (node.type == StructureType::Capital ? 55.f : 28.f) *
                           static_cast<float>(node.progress.to_double());
      const auto color = node.functional ? kingdom_color(node.owner) : Color{107, 108, 107, 255};
      DrawRectangle(static_cast<int>(p.x - 6), static_cast<int>(p.y - height), 12,
                    static_cast<int>(height), shade(color, .7f));
      diamond(p.x, p.y - height, 12, 6, color);
    }
}
void label(int x, int y, const char *text, int size = 16, Color color = INK) {
  DrawText(text, x, y, size, color);
}
} // namespace
int main(int argc, char **argv) {
  Config cfg;
  uint64_t seed = 1;
  int frames = 0;
  std::string screenshot;
  bool start_running = false;
  int start_speed = 1;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--run") {
      start_running = true;
      continue;
    }
    if (arg == "--help") {
      std::puts("ashfall_viewer [--config FILE] [--seed N] [--frames N] [--screenshot FILE] "
                "[--run] [--speed 1|4|16|64]\nWASD/drag "
                "pan; wheel zoom; R rotate; space pause; N step; 1..4 speed; O overlay; F fit");
      return 0;
    }
    if (i + 1 >= argc) {
      std::fprintf(stderr, "missing option value\n");
      return 2;
    }
    const std::string value = argv[++i];
    if (arg == "--config") {
      auto parsed = load_config(value);
      if (!parsed) {
        std::fprintf(stderr, "%s\n", parsed.error.c_str());
        return 2;
      }
      cfg = parsed.config;
    } else if (arg == "--screenshot")
      screenshot = value;
    else {
      uint64_t number = 0;
      auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), number);
      if (ec != std::errc{} || p != value.data() + value.size()) {
        std::fprintf(stderr, "invalid number\n");
        return 2;
      }
      if (arg == "--seed")
        seed = number;
      else if (arg == "--speed" && (number == 1 || number == 4 || number == 16 || number == 64))
        start_speed = static_cast<int>(number);
      else if (arg == "--frames" && number <= 100000)
        frames = static_cast<int>(number);
      else {
        std::fprintf(stderr, "unknown option\n");
        return 2;
      }
    }
  }
  World world(cfg, seed);
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
  InitWindow(1440, 960, "ASHFALL | Forest observatory");
  if (!IsWindowReady())
    return 1;
  SetWindowMinSize(1100, 900);
  SetTargetFPS(60);
  const int n = cfg.world.size, chunks = (n + CHUNK - 1) / CHUNK;
  std::vector<Cache> cache(static_cast<size_t>(chunks * chunks));
  auto invalidate = [&] {
    for (auto &c : cache)
      c.dirty = true;
  };
  int rotation = 0, overlay = 0;
  world.set_dirty_callback([&](int cx, int cy) {
    // Rotation remaps chunk indices exactly for N divisible by 32. Arbitrary N
    // can straddle rotated chunk boundaries, so invalidate all in that case.
    if (n % CHUNK) {
      invalidate();
      return;
    }
    const auto p = rotate(cx, cy, chunks, rotation);
    cache[static_cast<size_t>(static_cast<int>(p.y) * chunks + static_cast<int>(p.x))].dirty = true;
  });
  Camera2D camera{};
  camera.rotation = 0;
  auto fit = [&] {
    camera.offset = {static_cast<float>(GetScreenWidth() - 330) / 2, 170};
    camera.target = {0, 0};
    camera.zoom =
        std::min(static_cast<float>(GetScreenWidth() - 380) / (static_cast<float>(n) * 32),
                 static_cast<float>(GetScreenHeight() - 200) / (static_cast<float>(n) * 16));
  };
  fit();
  bool paused = !start_running;
  bool kingdom_panel = world.civilization() != nullptr;
  int speed = start_speed, selected = -1;
  uint64_t frame = 0;
  double simulation_credit = 0;
  double benchmark_start = GetTime();
  double sim_seconds = 0, raster_seconds = 0, present_seconds = 0;
  std::deque<FireEvent> recent;
  std::array<uint64_t, 20> histogram{};
  while (!WindowShouldClose()) {
    ++frame;
    if (IsKeyPressed(KEY_TAB))
      kingdom_panel = !kingdom_panel;
    if (IsKeyPressed(KEY_SPACE))
      paused = !paused;
    if (IsKeyPressed(KEY_ONE))
      speed = 1;
    if (IsKeyPressed(KEY_TWO))
      speed = 4;
    if (IsKeyPressed(KEY_THREE))
      speed = 16;
    if (IsKeyPressed(KEY_FOUR))
      speed = 64;
    if (IsKeyPressed(KEY_R)) {
      rotation = (rotation + 1) % 4;
      invalidate();
    }
    if (IsKeyPressed(KEY_O)) {
      overlay = (overlay + 1) % 3;
      invalidate();
    }
    if (IsKeyPressed(KEY_F) || IsWindowResized())
      fit();
    const Vector2 mouse = GetMousePosition();
    if (mouse.x < static_cast<float>(GetScreenWidth() - 330)) {
      const float wheel = GetMouseWheelMove();
      if (wheel != 0) {
        const Vector2 before = GetScreenToWorld2D(mouse, camera);
        camera.zoom = std::clamp(camera.zoom * (wheel > 0 ? 1.25f : .8f), .03f, 4.f);
        const Vector2 after = GetScreenToWorld2D(mouse, camera);
        camera.target.x += before.x - after.x;
        camera.target.y += before.y - after.y;
      }
      if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        const Vector2 delta = GetMouseDelta();
        camera.target.x -= delta.x / camera.zoom;
        camera.target.y -= delta.y / camera.zoom;
      }
    }
    const float pan = 600 * GetFrameTime() / camera.zoom;
    camera.target.x += pan * static_cast<float>(IsKeyDown(KEY_D) - IsKeyDown(KEY_A));
    camera.target.y += pan * static_cast<float>(IsKeyDown(KEY_S) - IsKeyDown(KEY_W));
    if (!paused)
      simulation_credit =
          std::min(128.0, simulation_credit + static_cast<double>(GetFrameTime()) * 16 * speed);
    else
      simulation_credit = 0;
    const bool single = IsKeyPressed(KEY_N);
    const int steps = single ? 1 : static_cast<int>(std::min(8.0, simulation_credit));

    const double sim_begin = GetTime();
    for (int s = 0; s < steps; ++s) {
      if (s > 0 && GetTime() - sim_begin > .004)
        break;
      world.tick();
      if (!single)
        simulation_credit -= 1;
      for (const auto &event : world.forest().events()) {
        recent.push_front(event);
        if (recent.size() > 6)
          recent.pop_back();
        size_t bin = 0;
        uint64_t size = event.size;
        while (size > 1 && bin + 1 < histogram.size()) {
          size >>= 1;
          ++bin;
        }
        ++histogram[bin];
      }
    }
    sim_seconds += GetTime() - sim_begin;
    if (steps > 0 && overlay == 2)
      invalidate();
    // Height-aware picking visits candidate diagonals front-to-back. Test the
    // actual top diamond rather than the z=0 inverse projection alone.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        mouse.x < static_cast<float>(GetScreenWidth() - 330) && mouse.y > 86) {
      const auto p = GetScreenToWorld2D(mouse, camera);
      const auto ground = unproject(p.x, p.y);
      selected = -1;
      if (world.civilization())
        for (auto node = world.civilization()->nodes().rbegin();
             node != world.civilization()->nodes().rend(); ++node)
          if (!node->destroyed) {
            const auto r = rotate(node->cell % n, node->cell / n, n, rotation),
                       screen = project(r.x, r.y,
                                        static_cast<float>(world.terrain().elevation[node->cell]) *
                                            .25f);
            const float height = (node->type == StructureType::Capital ? 55.f : 28.f) *
                                 static_cast<float>(node->progress.to_double());
            if (CheckCollisionPointRec(p,
                                       {screen.x - 12, screen.y - height - 6, 24, height + 12})) {
              selected = node->cell;
              break;
            }
          }
      for (int d = 20; d >= -2 && selected < 0; --d)
        for (int dy = -1; dy <= 1 && selected < 0; ++dy)
          for (int dx = -1; dx <= 1 && selected < 0; ++dx) {
            const int rx = static_cast<int>(std::floor(ground.x)) + d + dx,
                      ry = static_cast<int>(std::floor(ground.y)) + d + dy;
            if (rx < 0 || ry < 0 || rx >= n || ry >= n)
              continue;
            const auto o = original(rx, ry, n, rotation);
            const int x = static_cast<int>(o.x), y = static_cast<int>(o.y);
            const auto projected =
                project(static_cast<float>(rx), static_cast<float>(ry),
                        static_cast<float>(world.terrain().elevation.at(x, y)) * .25f);
            if (std::abs(p.x - projected.x) / 16 + std::abs(p.y - projected.y) / 8 <= 1)
              selected = y * n + x;
          }
    }
    // Keep only visible textures resident; bounded GPU memory even at N=1024.
    std::vector<int> visible;
    visible.reserve(cache.size());
    for (int d = 0; d < chunks * 2 - 1; ++d)
      for (int cx = std::max(0, d - chunks + 1); cx <= std::min(chunks - 1, d); ++cx) {
        const int cy = d - cx;
        const auto p = project(static_cast<float>(cx * CHUNK), static_cast<float>(cy * CHUNK));
        const auto screen = GetWorldToScreen2D({p.x - OX, p.y - OY}, camera);
        Rectangle bounds{screen.x, screen.y, TW * camera.zoom, TH * camera.zoom};
        if (CheckCollisionRecs(bounds, {0, 85, static_cast<float>(GetScreenWidth() - 330),
                                        static_cast<float>(GetScreenHeight() - 85)}))
          visible.push_back(cy * chunks + cx);
      }
    // At far zoom on huge maps use direct tiles, avoiding hundreds of textures.
    const bool use_cache = true;
    const float raster_scale =
        std::clamp(std::pow(2.f, std::ceil(std::log2(camera.zoom))), .03125f, 1.f);
    for (size_t index = 0; index < cache.size(); ++index)
      if (cache[index].texture.id &&
          (!use_cache ||
           std::find(visible.begin(), visible.end(), static_cast<int>(index)) == visible.end())) {
        UnloadRenderTexture(cache[index].texture);
        cache[index].texture = {};
        cache[index].dirty = true;
      }
    const double raster_begin = GetTime();
    auto refresh = visible;
    std::stable_sort(refresh.begin(), refresh.end(), [&](int a, int b) {
      return cache[static_cast<size_t>(a)].last < cache[static_cast<size_t>(b)].last;
    });
    int refreshed = 0;
    if (use_cache)
      for (int index : refresh) {
        auto &c = cache[static_cast<size_t>(index)];
        if (c.texture.id && c.scale != raster_scale) {
          UnloadRenderTexture(c.texture);
          c.texture = {};
        }
        if (!c.texture.id) {
          c.scale = raster_scale;
          c.texture = LoadRenderTexture(static_cast<int>(std::ceil(TW * c.scale)),
                                        static_cast<int>(std::ceil(TH * c.scale)));
          SetTextureFilter(c.texture.texture, TEXTURE_FILTER_POINT);
          c.dirty = true;
        }
        if (!c.dirty)
          continue;
        if (c.last > 0 && refreshed > 0 && GetTime() - raster_begin > .004)
          continue;
        const int cx = index % chunks, cy = index / chunks;
        BeginTextureMode(c.texture);
        ClearBackground(BLANK);
        Camera2D raster{};
        raster.zoom = c.scale;
        BeginMode2D(raster);
        for (int d = 0; d < CHUNK * 2 - 1; ++d)
          for (int lx = std::max(0, d - CHUNK + 1); lx <= std::min(CHUNK - 1, d); ++lx) {
            const int ly = d - lx, rx = cx * CHUNK + lx, ry = cy * CHUNK + ly;
            if (rx >= n || ry >= n)
              continue;
            const auto o = original(rx, ry, n, rotation),
                       p = project(static_cast<float>(lx), static_cast<float>(ly));
            cell(world, static_cast<int>(o.x), static_cast<int>(o.y), p.x + OX, p.y + OY, overlay);
          }
        EndMode2D();
        EndTextureMode();
        c.dirty = false;
        c.last = frame;
        ++refreshed;
      }
    raster_seconds += GetTime() - raster_begin;
    const double present_begin = GetTime();
    BeginDrawing();
    ClearBackground(BACKGROUND);
    BeginScissorMode(0, 85, GetScreenWidth() - 330, GetScreenHeight() - 120);
    BeginMode2D(camera);
    if (use_cache)
      for (int index : visible) {
        const auto p = project(static_cast<float>(index % chunks * CHUNK),
                               static_cast<float>(index / chunks * CHUNK));
        const auto texture = cache[static_cast<size_t>(index)].texture.texture;
        const float scale = cache[static_cast<size_t>(index)].scale;
        DrawTexturePro(
            texture, {0, 0, static_cast<float>(texture.width), -static_cast<float>(texture.height)},
            {p.x - OX, p.y - OY, static_cast<float>(texture.width) / scale,
             static_cast<float>(texture.height) / scale},
            {0, 0}, 0, WHITE);
      }
    else
      for (int d = 0; d < n * 2 - 1; ++d)
        for (int x = std::max(0, d - n + 1); x <= std::min(n - 1, d); ++x) {
          const int y = d - x;
          const auto o = original(x, y, n, rotation),
                     p = project(static_cast<float>(x), static_cast<float>(y));
          cell(world, static_cast<int>(o.x), static_cast<int>(o.y), p.x, p.y, overlay);
        }
    society(world, rotation);
    if (selected >= 0) {
      const auto r = rotate(selected % n, selected / n, n, rotation),
                 p = project(r.x, r.y,
                             static_cast<float>(world.terrain().elevation[selected]) * .25f);
      diamond(p.x, p.y, 16, 8, Fade(ACCENT, .7f));
    }
    EndMode2D();
    EndScissorMode();
    DrawRectangle(0, 0, GetScreenWidth(), 85, PANEL);
    label(26, 20, "A S H F A L L", 28, ACCENT);
    label(28, 55, world.civilization() ? "KINGDOMS / RISE AND COLLAPSE" : "FOREST OBSERVATORY", 12,
          MUTED);
    label(340, 25, TextFormat("SEED %llu     %d x %d", static_cast<unsigned long long>(seed), n, n),
          16);
    label(340, 51,
          TextFormat("TICK %llu   /   %s   %dx",
                     static_cast<unsigned long long>(world.current_tick()),
                     paused ? "PAUSED" : "RUNNING", speed),
          14, MUTED);
    const int panel = GetScreenWidth() - 330;
    DrawRectangle(panel, 85, 330, GetScreenHeight() - 85, PANEL);
    if (kingdom_panel && world.civilization()) {
      const auto &civ = *world.civilization();
      label(panel + 24, 110, "KINGDOMS", 18, ACCENT);
      int y = 145;
      for (const auto &k : civ.kingdoms()) {
        label(panel + 24, y,
              TextFormat("%02d  %s  / generation %d", k.id + 1, k.alive ? "ALIVE" : "COLLAPSED",
                         k.generation),
              15, kingdom_color(k.id));
        label(panel + 24, y + 20,
              TextFormat("Pop %.0f   Grain %.0f   Nodes %d/%d",
                         static_cast<double>(k.stats.population) / 65536,
                         static_cast<double>(k.stock[2]) / 65536, k.stats.functional,
                         k.stats.nodes),
              13, MUTED);
        y += 46;
      }
      y = std::max(360, y + 20);
      label(panel + 24, y, "CELL / DEPENDENCIES", 14, ACCENT);
      y += 30;
      if (selected >= 0) {
        label(
            panel + 24, y,
            TextFormat("(%d, %d)  owner %d", selected % n, selected / n, civ.owner()[selected] + 1),
            15);
        y += 28;
        for (const auto &node : civ.nodes())
          if (node.cell == selected && !node.destroyed) {
            label(panel + 24, y,
                  TextFormat("Node %d   type %d   %s", node.id, static_cast<int>(node.type),
                             node.functional ? "functional" : "failed"),
                  14);
            y += 26;
            label(panel + 24, y,
                  TextFormat("Integrity %.2f   Build %.2f", node.integrity.to_double(),
                             node.progress.to_double()),
                  13, MUTED);
            y += 26;
            for (int dependency : node.dependencies)
              if (dependency >= 0) {
                const auto &other = civ.nodes()[static_cast<size_t>(dependency)];
                label(
                    panel + 24, y,
                    TextFormat(" -> %d  %s", dependency, other.functional ? "supplied" : "failed"),
                    14, other.functional ? Color{132, 191, 119, 255} : Color{232, 116, 110, 255});
                y += 24;
              }
          }
      } else
        label(panel + 24, y, "Click a tile or structure to inspect", 13, MUTED);
      label(panel + 24, GetScreenHeight() - 65, "TAB: forest statistics", 13, MUTED);
    } else {
      label(panel + 24, 110, "THE LIVING LANDSCAPE", 16, ACCENT);
      const auto &m = world.forest().metrics();
      const double cover =
          m.land ? 100. * static_cast<double>(m.trees + m.saplings) / static_cast<double>(m.land)
                 : 0;
      label(panel + 24, 149, TextFormat("%.1f%%", cover), 42);
      label(panel + 24, 197, "forest coverage / land cells", 13, MUTED);
      DrawRectangle(panel + 24, 224, 280, 4, Color{42, 58, 60, 255});
      DrawRectangle(panel + 24, 224, static_cast<int>(cover * 2.8), 4, Color{101, 163, 104, 255});
      label(panel + 24, 251,
            TextFormat("%llu   mature trees", static_cast<unsigned long long>(m.trees)), 17);
      label(panel + 24, 281,
            TextFormat("%llu   burning now", static_cast<unsigned long long>(m.burning)), 17,
            Color{236, 138, 77, 255});
      label(panel + 24, 311,
            TextFormat("%llu   completed fires", static_cast<unsigned long long>(m.events)), 17);
      label(panel + 24, 359, "BURN SIZE / LOG-LOG CCDF", 13, ACCENT);
      uint64_t tail = 0;
      for (auto count : histogram)
        tail += count;
      const uint64_t total = tail;
      Vector2 last{};
      bool have = false;
      for (size_t b = 0; b < histogram.size(); ++b) {
        if (tail > 0 && total > 0) {
          const float x = static_cast<float>(panel + 28) + static_cast<float>(b) * 13;
          const float y = 392 - static_cast<float>(std::log10(static_cast<double>(tail) /
                                                              static_cast<double>(total))) *
                                    35;
          if (have)
            DrawLineEx(last, {x, y}, 2, ACCENT);
          last = {x, y};
          have = true;
        }
        tail -= histogram[b];
      }
      label(panel + 24, 488,
            total ? "1                 128            65k" : "Run simulation to collect samples",
            12, MUTED);
      label(panel + 24, 529, "CELL INSPECTOR", 13, ACCENT);
      if (selected >= 0) {
        const auto &t = world.terrain();
        const auto &f = world.forest();
        constexpr const char *names[] = {"empty", "sapling", "tree", "burning", "ash"};
        label(panel + 24, 559,
              TextFormat("(%d, %d)   %s / age %d", selected % n, selected / n,
                         names[f.state()[selected]], f.age()[selected]),
              16);
        label(panel + 24, 587,
              TextFormat("Elevation %d    Stone %d", t.elevation[selected], t.stone[selected]), 15,
              MUTED);
        label(panel + 24, 614,
              TextFormat("Moisture %.3f    Soil %.3f", t.moisture[selected].to_double(),
                         f.fertility()[selected].to_double()),
              15, MUTED);
      } else
        label(panel + 24, 562, "Click a ground tile to inspect", 14, MUTED);
      label(panel + 24, 660, "RECENT FIRES", 13, ACCENT);
      int row = 690;
      for (const auto &e : recent) {
        label(panel + 24, row,
              TextFormat("#%llu   %llu cells / %llu ticks", static_cast<unsigned long long>(e.id),
                         static_cast<unsigned long long>(e.size),
                         static_cast<unsigned long long>(e.end_tick - e.start_tick)),
              13, MUTED);
        row += 24;
      }
    }
    label(24, GetScreenHeight() - 27,
          "SPACE pause   N step   1-4 speed   WASD / drag pan   Wheel zoom   R rotate   O overlay  "
          " F fit",
          13, MUTED);
    label(panel + 24, GetScreenHeight() - 27,
          TextFormat("%d FPS  /  %s", GetFPS(),
                     overlay == 0   ? "FOREST"
                     : overlay == 1 ? "MOISTURE"
                                    : "FERTILITY"),
          13, ACCENT);
    EndDrawing();
    present_seconds += GetTime() - present_begin;
    if (frames > 0 && frame >= static_cast<uint64_t>(frames)) {
      if (!screenshot.empty()) {
        Image capture = LoadImageFromScreen();
        const bool saved = ExportImage(capture, screenshot.c_str());
        UnloadImage(capture);
        if (!saved)
          std::fprintf(stderr, "could not write screenshot: %s\n", screenshot.c_str());
      }
      break;
    }
  }
  std::printf("frames=%llu seconds=%.3f ticks=%llu\n", static_cast<unsigned long long>(frame),
              GetTime() - benchmark_start, static_cast<unsigned long long>(world.current_tick()));
  std::printf("simulation_ms=%.2f raster_ms=%.2f present_ms=%.2f\n",
              sim_seconds * 1000 / static_cast<double>(frame),
              raster_seconds * 1000 / static_cast<double>(frame),
              present_seconds * 1000 / static_cast<double>(frame));
  world.set_dirty_callback({});
  for (auto &c : cache)
    if (c.texture.id)
      UnloadRenderTexture(c.texture);
  CloseWindow();
  return 0;
}
