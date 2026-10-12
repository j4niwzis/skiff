// skiff.bind:entry -- a press and a kept answer delivered, outside a release
// build: by the walk made once (skiff.bind:runtime). A release build's are
// skiff.bind:press's, every node's type known (the module is skiff.bind:
// see bind.cc).
export module skiff.bind:entry;

import std;
import skiff.model;
import skiff.scene;
import :press;
import :runtime;

export namespace skiff::bind {
#ifdef SKIFF_ERASED_WALKS
// What a handler returned, kept: delivered along its path, sent up the
// frames there; what nothing around it takes, to the model's reactions, then
// to the parts given (rt::parts) -- the sink's place.
template <class M, class N, class S = detail::NoSink>
bool answer(N &root, M &model, const scene::HostWork::KeptAnswer &kept, S *sink = nullptr) {
  model.beginBatch();
  rt::Pressing op(rt::AnyModel::editable(model), rt::servedBy(sink));
  op.answer(scene::AnyNodeRef::of(root), kept);
  model.endBatch();
  return op.reached() && !op.missed();
}
// A press, delivered down its path to the node pressed, whose onPress()
// says what it asks for: done there and sent up the frames around it.
template <class M, class N, class S = detail::NoSink>
bool press(N &root, M &model, const scene::Path &path, S *sink = nullptr) {
  model.beginBatch();
  rt::Pressing op(rt::AnyModel::editable(model), rt::servedBy(sink));
  op.press(scene::AnyNodeRef::of(root), path);
  model.endBatch();
  return op.reached() && !op.missed();
}
#endif
} // namespace skiff::bind
