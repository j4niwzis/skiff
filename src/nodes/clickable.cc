export module skiff.nodes:clickable;

import std;
import skia;
import skiff.paint;
import skiff.scene;

namespace skiff::nodes {
using skiff::scene::Anchor;
using skiff::scene::Axes;
using skiff::scene::Drawable;
using skiff::scene::Easing;
using skiff::scene::Margin;
using skiff::scene::Spec;
} // namespace skiff::nodes

export namespace skiff::nodes {

template <class Action = skiff::scene::NoAction> class Clickable;

// Anything that reacts to a click: all of it but the action, which
// Clickable<Action> keeps as a member of its own type. Every Clickable is
// selected in a stylesheet as Clickable<>, whatever its action is.
class ClickableCore : public skiff::scene::TypedDrawable<Clickable<>> {
public:
  explicit ClickableCore(std::string label = {}) : fLabel(std::move(label)) {}

protected:
  bool acceptsInput() const override { return true; }
  [[nodiscard]] skiff::scene::Semantics semantics() const override {
    skiff::scene::Semantics out;
    out.fRole = skiff::scene::SemanticRole::kButton;
    out.fLabel = fLabel;
    out.fActions = {skiff::scene::SemanticAction::kFocus,
                    skiff::scene::SemanticAction::kActivate};
    return out;
  }
  bool onClick(float, float) override {
    this->activate();
    return true;
  }
  // What the screen wants done.
  virtual void activate() = 0;

  void onPointerEvent(skiff::scene::PointerEvent &event) override {
    using skiff::scene::EventPhase;
    using skiff::scene::PointerAction;
    if (event.fPhase != EventPhase::kTarget) {
      return;
    }
    switch (event.fAction) {
    case PointerAction::kDown:
      fArmed = true;
      event.handle();
      break;
    case PointerAction::kUp:
      if (std::exchange(fArmed, false) &&
          fBounds.contains(event.fX, event.fY)) {
        (void)this->onClick(event.fX, event.fY);
        event.handle();
      }
      break;
    case PointerAction::kCancel:
      fArmed = false;
      break;
    default:
      break;
    }
  }

private:
  std::string fLabel;
  bool fArmed = false;
};

template <class Action> class Clickable : public ClickableCore {
public:
  explicit Clickable(Action action, std::string label = {})
      : ClickableCore(std::move(label)), fAction(std::move(action)) {}
  explicit Clickable(std::string label = {})
    requires std::same_as<Action, skiff::scene::NoAction>
      : ClickableCore(std::move(label)) {}

protected:
  void activate() override { std::invoke(fAction); }

private:
  [[no_unique_address]] Action fAction{};
};
Clickable(const char *) -> Clickable<>;
Clickable(std::string) -> Clickable<>;

} // namespace skiff::nodes
