export module skiff.nodes:clickable;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// Anything that reacts to a click, with what it shows inside it. The action
// is a member of its own type; a rule for Clickable matches all of them.
template <class Action, class... Children>
class Clickable : public skiff::scene::Node {
public:
  Clickable(Action action, std::string label, Children... children)
      : fChildren(std::move(children)...), fAction(std::move(action)),
        fLabel(std::move(label)) {}

  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fChildren);
  }

  [[nodiscard]] bool acceptsInput() const {
    return skiff::scene::kActs<Action>;
  }
  [[nodiscard]] skiff::scene::Semantics semantics() const {
    skiff::scene::Semantics out;
    out.fRole = skiff::scene::SemanticRole::kButton;
    out.fLabel = fLabel;
    out.fActions = {skiff::scene::SemanticAction::kFocus,
                    skiff::scene::SemanticAction::kActivate};
    return out;
  }
  [[nodiscard]] bool onClick(float, float) {
    std::invoke(fAction);
    return true;
  }

  // Activates on release inside, not on the press: a press that turns into
  // a scroll must not have clicked.
  void onPointerEvent(skiff::scene::PointerEvent &event) {
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
          fState.fBounds.contains(event.fX, event.fY)) {
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

  std::tuple<Children...> fChildren;

private:
  [[no_unique_address]] Action fAction;
  std::string fLabel;
  bool fArmed = false;
};

} // namespace skiff::nodes
