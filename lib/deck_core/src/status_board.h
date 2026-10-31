#pragma once
#include <stdint.h>
#include <string.h>
#include "helper_proto.h"

struct StatusCard {
  uint8_t icon;
  char text[kCardTextMax + 1];
};

// Status text the Mac helper pushes (CARD), hardware-free. Slot 0 = top bar; slots 1..7
// rotate on strip line 2 every kRotateMs. A card with a ttl expires; empty text clears its
// slot; clear() (helper lost) empties everything. gen() changes whenever anything shown
// changes, so the glue repaints only then (resending the same text is free).
class StatusBoard {
 public:
  static constexpr uint32_t kRotateMs = 5000;

  void onCard(const CardMsg &c, uint32_t nowMs) {
    if (c.slot >= kCardSlots) return;
    Slot &s = slots_[c.slot];
    if (c.text[0] == 0) {
      s.live = false;
      refreshGens();
      return;
    }
    s.live = true;
    s.card.icon = c.icon;
    memcpy(s.card.text, c.text, sizeof s.card.text);
    s.expires = c.ttlS != 0;
    s.expiresAt = nowMs + static_cast<uint32_t>(c.ttlS) * 1000u;
    refreshGens();
  }

  void clear() {
    for (Slot &s : slots_) s.live = false;
    cur_ = 0;
    refreshGens();
  }

  void tick(uint32_t nowMs) {
    for (Slot &s : slots_)
      if (s.live && s.expires && static_cast<int32_t>(nowMs - s.expiresAt) >= 0) {
        s.live = false;
      }
    if (cur_ == 0 || !slots_[cur_].live) {
      uint8_t n = next(cur_);
      if (n != cur_) {
        cur_ = n;
        shownAt_ = nowMs;
      }
    } else if (nowMs - shownAt_ >= kRotateMs) {
      uint8_t n = next(cur_);
      shownAt_ = nowMs;
      cur_ = n;
    }
    refreshGens();
  }

  const StatusCard *top() const { return slots_[0].live ? &slots_[0].card : nullptr; }
  const StatusCard *current() const { return cur_ && slots_[cur_].live ? &slots_[cur_].card : nullptr; }

  uint8_t count() const {  // live rotating cards
    uint8_t n = 0;
    for (uint8_t i = 1; i < kCardSlots; i++) n += slots_[i].live;
    return n;
  }

  uint8_t index() const {  // 1-based position of current() among live cards, 0 if none
    if (!current()) return 0;
    uint8_t n = 0;
    for (uint8_t i = 1; i <= cur_; i++) n += slots_[i].live;
    return n;
  }

  uint8_t topGen() const { return topGen_; }
  uint8_t stripGen() const { return stripGen_; }

 private:
  struct Slot {
    bool live = false;
    bool expires = false;
    uint32_t expiresAt = 0;
    StatusCard card{};
  };

  // Next live rotating slot after `from` (wrapping); `from` itself if it is the only live
  // one; 0 if none.
  uint8_t next(uint8_t from) const {
    for (uint8_t k = 1; k < kCardSlots; k++) {
      uint8_t i = static_cast<uint8_t>((from + k - 1) % (kCardSlots - 1) + 1);
      if (slots_[i].live) return i;
    }
    return 0;
  }

  // Snapshots of what each region currently shows; a gen bumps only when its snapshot differs.
  struct View {
    bool live = false;
    StatusCard card{};
    uint8_t index = 0, count = 0;
    bool operator==(const View &o) const {
      return live == o.live && (!live || (card.icon == o.card.icon && strcmp(card.text, o.card.text) == 0)) &&
             index == o.index && count == o.count;
    }
  };

  void refreshGens() {
    View t;
    if (top()) {
      t.live = true;
      t.card = *top();
    }
    if (!(t == top_)) {
      top_ = t;
      topGen_++;
    }
    View s;
    if (current()) {
      s.live = true;
      s.card = *current();
    }
    s.index = index();
    s.count = count();
    if (!(s == strip_)) {
      strip_ = s;
      stripGen_++;
    }
  }

  Slot slots_[kCardSlots];
  uint8_t cur_ = 0;
  uint32_t shownAt_ = 0;
  View top_, strip_;
  uint8_t topGen_ = 0, stripGen_ = 0;
};
