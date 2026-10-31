local X = require("saucedeck.text")
local t = {}
t.plain_ascii_kept = function(T) T.eq(X.clean("Standup 10:30"), "Standup 10:30") end
t.nil_is_empty = function(T) T.eq(X.clean(nil), "") end
t.accents_folded = function(T)
  T.eq(X.clean("Caf\xC3\xA9 cr\xC3\xA8me \xC3\xA0 Gen\xC3\xA8ve"), "Cafe creme a Geneve")
  T.eq(X.clean("\xC3\x89t\xC3\xA9 \xC3\x87a \xC3\xB1 \xC3\xBC\xC3\xB6\xC3\xA4\xC3\xAE\xC3\xB4\xC3\xBB\xC3\xAA\xC3\xAB\xC3\xAF"), "Ete Ca n uoaioueei")
end
t.punctuation_mapped = function(T)
  T.eq(X.clean("It\xE2\x80\x99s \xE2\x80\x93 ok\xE2\x80\xA6"), "It's - ok...")
end
t.degree_and_emoji_dropped = function(T)
  T.eq(X.clean("Patchy rain nearby +13\xC2\xB0C"), "Patchy rain nearby +13C")
  T.eq(X.clean("\xF0\x9F\x8E\xB5 Song"), "Song")
end
t.control_chars_become_space_and_collapse = function(T)
  T.eq(X.clean("  a\tb\n\nc\r  "), "a b c")
end
t.cut_to_48 = function(T)
  T.eq(#X.clean(string.rep("y", 60)), 48)
end
t.unsafe_bytes_dropped = function(T) T.eq(X.clean("a\x7Fb\xFFc"), "abc") end
return t
