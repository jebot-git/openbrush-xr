// SPDX-License-Identifier: Apache-2.0
#include "interaction.h"
namespace ob {
int Interaction::StickRepeat::step(float x, float y, float seconds) {
  if (blocked) {
    if (std::abs(x) < .2f && std::abs(y) < .2f)
      blocked = false;
    return 0;
  }
  int next = std::abs(x) > .55f && std::abs(x) > std::abs(y) * 1.15f
                 ? (x > 0 ? 1 : -1)
                 : 0;
  if (next != direction) {
    direction = next;
    remaining = .4f;
    return next;
  }
  if (!direction)
    return 0;
  remaining -= seconds;
  if (remaining <= 0) {
    remaining = .18f;
    return direction;
  }
  return 0;
}
void Interaction::shortcuts(const SessionControls &controls, bool right) {
  if (!controls.touch) {
    brushRepeat.reset();
    colorRepeat.reset();
    return;
  }
  if (controls.eraser) {
    editor.finish(2);
    auto value = editor.tools();
    if (value.tool == Tool::Erase)
      value.tool = previousTool;
    else {
      previousTool = value.tool;
      value.tool = Tool::Erase;
    }
    editor.configure(value);
    suppressRight = right;
    if (hands[0].trigger > .08f)
      suppressLeft = true;
    pulse(0, .25f);
  }
  if (controls.mirror) {
    editor.finish(2);
    auto value = editor.tools();
    value.mirror = !value.mirror;
    editor.configure(value);
    suppressRight = right;
    brushRepeat.block();
    pulse(0, .22f);
  }
  if (controls.recenter)
    colorRepeat.block();
  bool busy = false;
  for (auto &hand : hands)
    busy |= hand.trigger > .08f || hand.squeeze > .35f;
  if (busy) {
    brushRepeat.block();
    colorRepeat.block();
    return;
  }
  float dt = std::clamp(controls.seconds, 0.f, .05f);
  int brushStep = brushRepeat.step(controls.leftX, controls.leftY, dt),
      colorStep = colorRepeat.step(controls.rightX, controls.rightY, dt);
  auto value = editor.tools();
  bool change = false;
  if (brushStep) {
    int index = 0;
    for (size_t i = 0; i < brushes().size(); ++i)
      if (sameBrush(brushes()[i].guid, value.brush))
        index = i;
    index = (index + brushStep + brushes().size()) % brushes().size();
    value.brush = brushes()[index].guid;
    value.tool = Tool::Paint;
    change = true;
    pulse(0, .15f, 9);
  }
  if (colorStep) {
    auto index = (int(closestPreset(value.color)) + colorStep + 8) % 8;
    auto alpha = value.color.a;
    value.color = presetColors()[index];
    value.color.a = alpha;
    change = true;
    pulse(1, .15f, 9);
  }
  auto vertical = [](float x, float y) {
    return std::abs(y) > .25f && std::abs(y) > std::abs(x) * 1.15f
               ? std::copysign((std::abs(y) - .25f) / .75f, y)
               : 0.f;
  };
  float sizeRate =
            brushRepeat.blocked ? 0 : vertical(controls.leftX, controls.leftY),
        alphaRate = colorRepeat.blocked
                        ? 0
                        : vertical(controls.rightX, controls.rightY);
  if (sizeRate) {
    auto &size = value.tool == Tool::Paint ? value.size : value.eraser;
    float next =
        std::clamp(size * std::exp(sizeRate * dt * .69314718f), .001f, .15f);
    change |= next != size;
    size = next;
  }
  if (alphaRate) {
    float next = std::clamp(value.color.a + alphaRate * dt * .5f, .05f, 1.f);
    change |= next != value.color.a;
    value.color.a = next;
  }
  if (change)
    editor.configure(value);
}
Interaction::Interaction(Editor &e, std::function<void()> onChanged,
                         std::function<void()> onSave)
    : editor(e), changed(std::move(onChanged)), save(std::move(onSave)) {}
void Interaction::finish() {
  auto version = editor.frame().revision;
  editor.finish(2);
  navigation.reset();
  palette.cancel();
  if (editor.frame().revision != version && changed)
    changed();
}
void Interaction::update(const std::array<TrackedHand, 2> &input,
                         Transform head, SessionControls controls) {
  auto before = editor.frame().revision;
  hands = input;
  auto notify = [&] {
    if (editor.frame().revision != before && changed)
      changed();
  };
  bool right = hands[1].valid && hands[1].trigger > .08f;
  if (!hands[0].valid)
    suppressLeft = true;
  else if (hands[0].trigger <= .08f)
    suppressLeft = false;
  if (hands[1].trigger <= .08f)
    moveRightCaptured = false;
  if (!hands[1].valid)
    suppressRight = true;
  else if (hands[1].trigger <= .08f)
    suppressRight = false;
  if (!controls.focused) {
    editor.finish(2);
    navigation.reset();
    brushRepeat.block();
    colorRepeat.block();
    palette.cancel();
    suppressRight = true;
    hands = {};
    notify();
    return;
  }
  if (!hands[0].valid) {
    controls.leftX = controls.leftY = 0;
    controls.eraser = controls.mirror = false;
  }
  if (!hands[1].valid) {
    controls.rightX = controls.rightY = 0;
    controls.undo = controls.redo = controls.recenter = false;
  }
  shortcuts(controls, right);
  if (!hands[0].valid)
    brushRepeat.block();
  if (!hands[1].valid)
    colorRepeat.block();
  palette.setViewer(head);
  std::optional<Transform> left;
  if (hands[0].valid)
    left = hands[0].grip;
  if (!palette.visible()) {
    // place() anchors the initially visible palette once; an explicitly hidden
    // palette stays hidden until its toggle action is pressed.
    palette.ensurePlaced(head, left);
  }
  if (controls.recenter) {
    editor.finish(2);
    palette.showAt(head, left);
    suppressRight = right;
    pulse(1);
  }
  if (controls.palette) {
    editor.finish(2);
    palette.toggle(head, left);
    pulse(0);
    suppressRight = right;
  }
  if (controls.undo || controls.redo) {
    palette.cancel();
    editor.finish(2);
    bool applied = controls.undo ? editor.undo() : editor.redo();
    if (applied)
      pulse(1, .25f);
    if (hands[0].trigger > .08f)
      suppressLeft = true;
    suppressRight = right;
  }
  if (moveRightCaptured && editor.tools().tool != Tool::Navigate && right)
    suppressRight = true;
  bool physicalGrab = false;
  for (auto &hand : hands)
    physicalGrab |= hand.valid && hand.squeeze > .55f;
  if (suppressRight && right) {
    palette.cancel();
    editor.finish(2);
    notify();
    return;
  }
  if (!physicalGrab) {
    auto clicks = palette.clickCount();
    bool consumed = palette.point(
        hands[1].aim.position, rotate(hands[1].aim.orientation, {0, 0, -1}),
        right, hands[1].valid && hands[1].aimValid, editor,
        save ? save : changed, hands[1].trigger);
    if (palette.clickCount() != clicks)
      pulse(1, .18f, 12);
    if (consumed) {
      navigation.reset();
      editor.finish(2);
      notify();
      return;
    }
  }
  auto navigationHands = hands;
  bool moveTool = editor.tools().tool == Tool::Navigate;
  if (moveTool)
    for (auto &hand : navigationHands)
      hand.squeeze = std::max(hand.squeeze, hand.trigger);
  auto move = navigation.update(navigationHands, editor.canvas());
  if (navigation.grabbing()) {
    editor.finish(2);
    if (move)
      editor.navigate(*move);
    palette.cancel();
    // Physical grip release must not turn an already-held trigger into paint.
    // Trigger-based Move gestures instead retain their pressure between frames.
    if (hands[0].trigger > .08f)
      suppressLeft = true;
    if (moveTool && right)
      moveRightCaptured = true;
    if (physicalGrab)
      suppressRight = right;
    notify();
    return;
  }
  if (moveTool) {
    editor.finish(2);
    notify();
    return;
  }
  if (!suppressLeft && hands[0].valid && hands[0].trigger > .08f) {
    editor.finish(2);
    editor.eraseRoom(hands[0].grip.position, editor.tools().eraser);
  } else if (right) {
    auto &hand = hands[1];
    Point p{hand.grip.position, hand.grip.orientation, hand.trigger,
            hand.timeMs};
    editor.sampleRoom(2, p);
  } else
    editor.finish(2);
  notify();
}
std::vector<Stroke> Interaction::visuals() const {
  std::vector<Stroke> result;
  if (navigation.grabbing())
    return result;
  auto settings = editor.tools();
  bool onPanel = false;
  if (palette.visible() && hands[1].valid && hands[1].aimValid) {
    auto origin = hands[1].aim.position,
         direction = rotate(hands[1].aim.orientation, {0, 0, -1});
    auto hit = palette.hit(origin, direction);
    auto end = hit ? hit->roomPoint : origin + direction * .6f;
    onPanel = bool(hit);
    if (palette.expanded() || hit) {
      Stroke ray;
      ray.brush = "2241cd32-8ba2-48a5-9ee7-2caef7e9ed62";
      ray.color = hit ? Color{.4f, .75f, 1, 1} : Color{.4f, .6f, .8f, .5f};
      ray.size = .0008f;
      ray.points = {{origin, {}, 1, 0}, {end, {}, 1, 0}};
      result.push_back(std::move(ray));
      if (hit) {
        for (auto axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}}) {
          auto delta = rotate(palette.transform().orientation, axis * .003f);
          Stroke mark;
          mark.brush = "2241cd32-8ba2-48a5-9ee7-2caef7e9ed62";
          mark.size = .001f;
          mark.color = {1, 1, 1, 1};
          mark.points = {{end - delta, {}, 1, 0}, {end + delta, {}, 1, 0}};
          result.push_back(std::move(mark));
        }
      }
    }
  }
  auto cursor = [&](const TrackedHand &hand, float radius, Color color) {
    for (int plane = 0; plane < 3; ++plane) {
      Stroke ring;
      ring.brush = "2241cd32-8ba2-48a5-9ee7-2caef7e9ed62";
      ring.size = std::clamp(radius * .035f, .0003f, .001f);
      ring.color = color;
      for (int i = 0; i <= 24; ++i) {
        float t = i * 6.283185307f / 24;
        Vec3 p = plane == 0   ? Vec3{std::cos(t), std::sin(t), 0}
                 : plane == 1 ? Vec3{std::cos(t), 0, std::sin(t)}
                              : Vec3{0, std::cos(t), std::sin(t)};
        ring.points.push_back(
            {hand.grip.position + rotate(hand.grip.orientation, p * radius),
             {},
             1,
             0});
      }
      result.push_back(std::move(ring));
    }
  };
  if (hands[0].valid && hands[0].trigger > .08f)
    cursor(hands[0], settings.eraser, {1, .2f, .2f, .6f});
  else if (hands[1].valid && !onPanel) {
    auto color = settings.tool == Tool::Paint   ? settings.color
                 : settings.tool == Tool::Erase ? Color{1, .2f, .2f, 1}
                                                : Color{1, .9f, .2f, 1};
    color.a = .6f;
    cursor(hands[1],
           settings.tool == Tool::Paint ? settings.size * .5f : settings.eraser,
           color);
  }
  return result;
}
} // namespace ob
