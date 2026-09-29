export module skiff.nodes.clickable;

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
    return skiff::scene::acts(fAction);
  }
  [[nodiscard]] skiff::scene::Semantics semantics() const {
    skiff::scene::Semantics out;
    out.fRole = skiff::scene::semantic_role::button{};
    out.fLabel = fLabel;
    out.fActions = {skiff::scene::semantic_action::focus{},
                    skiff::scene::semantic_action::activate{}};
    return out;
  }
  [[nodiscard]] bool onClick(float, float) {
    std::invoke(fAction);
    return true;
  }

  // Activates on release inside, not on the press: a press that turns into
  // a scroll must not have clicked.
  using Node::onPointer;
  void onPointer(skiff::scene::phase::target, const skiff::scene::pointer::down &,
                 skiff::scene::PointerReply &reply) {
    fArmed = true;
    reply.handle();
  }
  void onPointer(skiff::scene::phase::target,
                 const skiff::scene::pointer::up &release,
                 skiff::scene::PointerReply &reply) {
    if (std::exchange(fArmed, false) &&
        fState.fBounds.contains(release.x, release.y)) {
      (void)this->onClick(release.x, release.y);
      reply.handle();
    }
  }
  void onPointer(skiff::scene::phase::target, const skiff::scene::pointer::cancel &,
                 skiff::scene::PointerReply &) {
    fArmed = false;
  }

  std::tuple<Children...> fChildren;

private:
  [[no_unique_address]] Action fAction;
  std::string fLabel;
  bool fArmed = false;
};

} // namespace skiff::nodes
