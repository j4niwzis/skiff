// A page on skiff.web: the settings of a maze visualizer (Kirkezz/Mazes), as
// one model and one tree -- what its Qt window has, in a browser, with every
// control a native one. Built with Emscripten; the page is index.html.
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
import std;
import skiff.core;
import skiff.model;
import skiff.bind;
import skiff.web;

using namespace skiff;
using namespace skiff::web;

enum class Tiling { kOrthogonal, kHexagonal, kVoronoiRandom, kVoronoiPoints };
enum class Generator { kNone, kRecursiveBacktracker, kEllers, kKruskals, kPrims, kRecursiveDivision, kAldousBroder, kWilsons, kHuntAndKill, kGrowingTree };
enum class Pathfinder { kBreadthFirst, kAStar };

struct Grid {
  Tiling tiling = Tiling::kOrthogonal;
  int columns = 16;
  int rows = 16;
  int cellWidth = 48;
  int cellHeight = 48;
};
struct Maze {
  Generator generator = Generator::kRecursiveBacktracker;
  Pathfinder pathfinder = Pathfinder::kBreadthFirst;
  int aisleWidth = 1;
  float ellersVertical = 0.5f;
};
struct Visualization {
  bool visualize = true;
  bool sonify = false;
  float mazeDelay = 0.0f;
  float pathDelay = 0.0f;
  float pointSpeed = 8.0f;
  bool grid = true, edges = true, path = true, dots = true, walls = true, debug = false;
};
struct Settings {
  Grid grid;
  Maze maze;
  Visualization visualization;
  int applied = 0;
};
struct Root {
  model::Tracked<Settings> settings;
};
struct Reactions {};
using Model = model::Model<Root, Reactions>;

struct Apply {};

// A text showing what a callable makes of a part, as the part moves.
template <class T, class F> class Label : public Text {
public:
  explicit Label(F format, scene::Spec spec = {}) : Text(std::string(), std::move(spec)), fFormat(std::move(format)) {}
  void read(const T &now) { setText(fFormat(now)); }

private:
  F fFormat;
};
template <class T, class F> Label<T, F> label(F format, scene::Spec spec = {}) { return Label<T, F>(std::move(format), std::move(spec)); }

template <class... Parts> auto group(std::string title, Parts... parts) {
  return column(vbox(6.0f, {.padding = scene::Margin::all(10.0f), .cornerRadius = 6.0f, .background = 0xff2b2d33u}),
                Text(std::move(title), {.alpha = 0.75f}), std::move(parts)...);
}
template <class Control> auto field(std::string name, Control control) {
  return row(hbox(10.0f), Text(std::move(name), {.width = 170.0f}), std::move(control));
}
// Two to a row: each takes half, its label growing to push the control to
// the half's end.
template <class Control> auto check(std::string name, Control control) {
  return row(hbox(8.0f, {.fill = true}), Text(std::move(name), {.grow = scene::axes::kX}), std::move(control));
}

auto page() {
  return scoped<Root>(
      handlers(handle<Apply>([](const Apply &, const auto &) {
        return model::over<Settings>([](Settings s) {
          ++s.applied;
          return s;
        });
      })),
      column(vbox(10.0f, {.padding = scene::Margin::all(14.0f), .width = 448.0f}),
             group("Grid geometry",
                   field("Tiling", bound<model::Field<&Grid::tiling>>(Dropdown<Tiling>({{"Orthogonal", Tiling::kOrthogonal},
                                                                                        {"Hexagonal", Tiling::kHexagonal},
                                                                                        {"Voronoi diagram (random)", Tiling::kVoronoiRandom},
                                                                                        {"Voronoi diagram (points)", Tiling::kVoronoiPoints}}))),
                   field("Columns", bound<model::Field<&Grid::columns>>(SliderField<int>(1, 128))),
                   field("Rows", bound<model::Field<&Grid::rows>>(SliderField<int>(1, 128))),
                   field("Cell width", bound<model::Field<&Grid::cellWidth>>(SliderField<int>(8, 128))),
                   field("Cell height", bound<model::Field<&Grid::cellHeight>>(SliderField<int>(8, 128)))),
             group("Maze settings",
                   field("Generation algorithm",
                         bound<model::Field<&Maze::generator>>(Dropdown<Generator>({{"", Generator::kNone},
                                                                                     {"Recursive backtracker", Generator::kRecursiveBacktracker},
                                                                                     {"Eller's algorithm", Generator::kEllers},
                                                                                     {"Randomized Kruskal's algorithm", Generator::kKruskals},
                                                                                     {"Prim's algorithm", Generator::kPrims},
                                                                                     {"Recursive Division", Generator::kRecursiveDivision},
                                                                                     {"Aldous-Broder algorithm", Generator::kAldousBroder},
                                                                                     {"Wilson's algorithm", Generator::kWilsons},
                                                                                     {"Hunt-and-Kill", Generator::kHuntAndKill},
                                                                                     {"Growing tree algorithm", Generator::kGrowingTree}}))),
                   field("Aisle width", bound<model::Field<&Maze::aisleWidth>>(SliderField<int>(1, 8))),
                   field("Eller's vertical probability", bound<model::Field<&Maze::ellersVertical>>(SliderField<float>(0.0f, 1.0f))),
                   field("Pathfinding algorithm",
                         bound<model::Field<&Maze::pathfinder>>(ChoiceTabs<Pathfinder>({{"Breadth-first search", Pathfinder::kBreadthFirst}, {"Algorithm A*", Pathfinder::kAStar}}))),
                   SendButton<Apply>("Apply", Apply{})),
             group("Visualization",
                   field("Visualize", bound<model::Field<&Visualization::visualize>>(ToggleField())),
                   field("Sonify", bound<model::Field<&Visualization::sonify>>(ToggleField())),
                   field("Maze generation delay", bound<model::Field<&Visualization::mazeDelay>>(SliderField<float>(0.0f, 2.0f))),
                   field("Pathfinding delay", bound<model::Field<&Visualization::pathDelay>>(SliderField<float>(0.0f, 2.0f))),
                   field("Speed of points", bound<model::Field<&Visualization::pointSpeed>>(SliderField<float>(1.0f, 64.0f)))),
             group("Graphic elements",
                   row(hbox(16.0f), check("Grid", bound<model::Field<&Visualization::grid>>(ToggleField())),
                       check("Edges of a maze graph", bound<model::Field<&Visualization::edges>>(ToggleField()))),
                   row(hbox(16.0f), check("Shortest path", bound<model::Field<&Visualization::path>>(ToggleField())),
                       check("Dots", bound<model::Field<&Visualization::dots>>(ToggleField()))),
                   row(hbox(16.0f), check("Maze walls", bound<model::Field<&Visualization::walls>>(ToggleField())),
                       check("Debug info", bound<model::Field<&Visualization::debug>>(ToggleField())))),
             bound<Settings>(label<Settings>(
                 [](const Settings &s) {
                   return std::format("{}x{} cells of {}x{}px, generator #{}, aisle {}, {} · applied {} time(s)", s.grid.columns, s.grid.rows,
                                      s.grid.cellWidth, s.grid.cellHeight, static_cast<int>(s.maze.generator), s.maze.aisleWidth,
                                      s.visualization.visualize ? "visualized" : "instant", s.applied);
                 },
                 {.alpha = 0.8f}))));
}

namespace {
Model gModel;
using Tree = decltype(page());
std::optional<Page<Tree, Model>> gPage;
} // namespace

#ifdef __EMSCRIPTEN__
// The document's listeners' way in: the page's type known here, nothing erased.
extern "C" EMSCRIPTEN_KEEPALIVE void skiff_web_event(std::uint32_t handle, const char *event, const char *value) {
  if (gPage) gPage->event(handle, std::string_view(event), std::string_view(value));
}
#endif

int main() {
  gPage.emplace(page(), gModel);
  gPage->mount();
#ifndef __EMSCRIPTEN__
  // Built with the in-memory document: the page as HTML, to look at or test.
  std::println("{}", dom::dump(dom::root()));
#endif
  return 0;
}
